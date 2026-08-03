//===- DeclCXX.cpp - C++ Declaration nodes --------------------------------===//
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Type/Type.h"

using namespace neverc;

//===----------------------------------------------------------------------===//
// CXXRecordDecl
//===----------------------------------------------------------------------===//

CXXRecordDecl::CXXRecordDecl(const TreeContext &C, TagKind TK, DeclContext *DC,
                             SourceLocation StartLoc, SourceLocation IdLoc,
                             IdentifierInfo *Id, CXXRecordDecl *PrevDecl)
    : RecordDecl(CXXRecord, TK, C, DC, StartLoc, IdLoc, Id, PrevDecl) {}

CXXRecordDecl *CXXRecordDecl::Create(const TreeContext &C, TagKind TK,
                                     DeclContext *DC, SourceLocation StartLoc,
                                     SourceLocation IdLoc, IdentifierInfo *Id,
                                     CXXRecordDecl *PrevDecl,
                                     bool DelayTypeCreation) {
  auto *R = new (C, DC)
      CXXRecordDecl(C, TK, DC, StartLoc, IdLoc, Id, PrevDecl);
  if (!DelayTypeCreation)
    C.getTypeDeclType(R, PrevDecl);
  return R;
}

void CXXRecordDecl::startDefinition() {
  TagDecl::startDefinition();
  if (!DefinitionDataPtr) {
    void *Mem = getTreeContext().Allocate(sizeof(DefinitionData));
    DefinitionDataPtr = new (Mem) DefinitionData();
  }
}

void CXXRecordDecl::completeDefinition() {
  TagDecl::completeDefinition();
}

//===----------------------------------------------------------------------===//
// CXXMethodDecl
//===----------------------------------------------------------------------===//

CXXMethodDecl *CXXMethodDecl::Create(TreeContext &C, CXXRecordDecl *RD,
                                     SourceLocation StartLoc,
                                     const DeclarationNameInfo &NameInfo,
                                     QualType T, TypeSourceInfo *TInfo,
                                     StorageClass SC, bool UsesFPIntrin,
                                     bool isInline,
                                     ConstexprSpecKind ConstexprKind) {
  return new (C, RD) CXXMethodDecl(CXXMethod, C, RD, StartLoc, NameInfo, T,
                                   TInfo, SC, UsesFPIntrin, isInline,
                                   ConstexprKind);
}

//===----------------------------------------------------------------------===//
// CXXConstructorDecl
//===----------------------------------------------------------------------===//

void CXXConstructorDecl::anchor() {}

CXXConstructorDecl::CXXConstructorDecl(
    TreeContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    bool isExplicit, bool isInline, bool isImplicitlyDeclared,
    ConstexprSpecKind ConstexprKind)
    : CXXMethodDecl(CXXConstructor, C, RD, StartLoc, NameInfo, T, TInfo,
                    SC_None, false, isInline, ConstexprKind) {
  (void)isExplicit;
  setImplicit(isImplicitlyDeclared);
}

CXXConstructorDecl *CXXConstructorDecl::Create(
    TreeContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    bool isExplicit, bool isInline, bool isImplicitlyDeclared,
    ConstexprSpecKind ConstexprKind) {
  return new (C, RD) CXXConstructorDecl(C, RD, StartLoc, NameInfo, T, TInfo,
                                        isExplicit, isInline,
                                        isImplicitlyDeclared, ConstexprKind);
}

bool CXXConstructorDecl::isDefaultConstructor() const {
  return getNumParams() == 0 ||
         (getNumParams() > 0 && getParamDecl(0)->hasDefaultArg());
}

bool CXXConstructorDecl::isCopyConstructor() const {
  if (getNumParams() == 0)
    return false;
  QualType ParamTy = getParamDecl(0)->getType();
  if (const auto *RT = ParamTy->getAs<LValueReferenceType>()) {
    QualType Pointee = RT->getPointeeType().getUnqualifiedType();
    return Pointee->getAsCXXRecordDecl() == getParent();
  }
  return false;
}

bool CXXConstructorDecl::isMoveConstructor() const {
  if (getNumParams() == 0)
    return false;
  QualType ParamTy = getParamDecl(0)->getType();
  if (const auto *RT = ParamTy->getAs<RValueReferenceType>()) {
    QualType Pointee = RT->getPointeeType().getUnqualifiedType();
    return Pointee->getAsCXXRecordDecl() == getParent();
  }
  return false;
}

//===----------------------------------------------------------------------===//
// CXXDestructorDecl
//===----------------------------------------------------------------------===//

void CXXDestructorDecl::anchor() {}

CXXDestructorDecl::CXXDestructorDecl(TreeContext &C, CXXRecordDecl *RD,
                                     SourceLocation StartLoc,
                                     const DeclarationNameInfo &NameInfo,
                                     QualType T, TypeSourceInfo *TInfo,
                                     bool isInline, bool isImplicitlyDeclared)
    : CXXMethodDecl(CXXDestructor, C, RD, StartLoc, NameInfo, T, TInfo, SC_None,
                    false, isInline, ConstexprSpecKind::Unspecified) {
  setImplicit(isImplicitlyDeclared);
}

CXXDestructorDecl *CXXDestructorDecl::Create(
    TreeContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    bool isInline, bool isImplicitlyDeclared) {
  return new (C, RD) CXXDestructorDecl(C, RD, StartLoc, NameInfo, T, TInfo,
                                       isInline, isImplicitlyDeclared);
}

//===----------------------------------------------------------------------===//
// CXXConversionDecl
//===----------------------------------------------------------------------===//

void CXXConversionDecl::anchor() {}

CXXConversionDecl::CXXConversionDecl(TreeContext &C, CXXRecordDecl *RD,
                                     SourceLocation StartLoc,
                                     const DeclarationNameInfo &NameInfo,
                                     QualType T, TypeSourceInfo *TInfo,
                                     bool isInline, bool isExplicit,
                                     ConstexprSpecKind ConstexprKind)
    : CXXMethodDecl(CXXConversion, C, RD, StartLoc, NameInfo, T, TInfo, SC_None,
                    false, isInline, ConstexprKind) {
  (void)isExplicit;
}

CXXConversionDecl *CXXConversionDecl::Create(
    TreeContext &C, CXXRecordDecl *RD, SourceLocation StartLoc,
    const DeclarationNameInfo &NameInfo, QualType T, TypeSourceInfo *TInfo,
    bool isInline, bool isExplicit, ConstexprSpecKind ConstexprKind) {
  return new (C, RD) CXXConversionDecl(C, RD, StartLoc, NameInfo, T, TInfo,
                                       isInline, isExplicit, ConstexprKind);
}
