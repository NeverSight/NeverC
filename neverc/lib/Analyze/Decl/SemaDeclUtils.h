//===- SemaDeclUtils.h - Shared helpers for split SemaDecl TUs ------------===//
//
// Internal header that exposes helpers originally file-local inside
// SemaDecl*.cpp. Splitting declaration checking into per-topic TUs
// requires these symbols to be visible across translation units.
//
//===----------------------------------------------------------------------===//

#ifndef NEVERC_LIB_ANALYZE_DECL_SEMADECLUTILS_H
#define NEVERC_LIB_ANALYZE_DECL_SEMADECLUTILS_H

#include "neverc/Analyze/Sema.h"
#include "neverc/Foundation/Builtin/Builtins.h"

namespace neverc {

bool isOutOfScopePreviousDeclaration(NamedDecl *PrevDecl, DeclContext *DC,
                                     TreeContext &Context);

void attachNeverCStringCleanup(Sema &S, Scope *Sc, VarDecl *VD,
                               FunctionDecl *ParamOwner = nullptr,
                               bool IsFunctionDefinitionParam = false);

void attachNeverCWptrCleanup(Sema &S, Scope *Sc, VarDecl *VD);

bool canOverloadFunction(const LookupResult &Previous, TreeContext &Context,
                         const FunctionDecl *New);

llvm::StringRef getHeaderName(Builtin::Context &BuiltinInfo, unsigned ID,
                              TreeContext::GetBuiltinTypeError Error);

bool canRedefineFunction(const FunctionDecl *FD, const LangOptions &LangOpts);

bool hasMatchingAttr(const Decl *D, const Attr *A);

void mergeParamDeclAttributes(ParmVarDecl *newDecl, const ParmVarDecl *oldDecl,
                              Sema &S);

bool equivalentArrayTypes(QualType Old, QualType New, const TreeContext &Ctx);

void mergeParamDeclTypes(ParmVarDecl *NewParam, const ParmVarDecl *OldParam,
                         Sema &S);

bool hasIdenticalPassObjectSizeAttrs(const FunctionDecl *A,
                                     const FunctionDecl *B);

void checkAttributesAfterMerging(Sema &S, NamedDecl &ND);

DeclContext *getTagInjectionContext(DeclContext *DC);
Scope *getTagInjectionScope(Scope *S, const LangOptions &LangOpts);

bool shouldIgnoreForRecordTriviality(const FieldDecl *FD);

StorageClass storageClassSpecToVarDeclStorageClass(const DeclSpec &DS);

QualType tryToFixInvalidVariablyModifiedType(QualType T, TreeContext &Context,
                                             bool &SizeIsNegative,
                                             llvm::APSInt &Oversized);

void fixInvalidVariablyModifiedTypeLoc(TypeLoc SrcTL, TypeLoc DstTL);

TypeSourceInfo *tryToFixInvalidVariablyModifiedTypeSourceInfo(
    TypeSourceInfo *TInfo, TreeContext &Context, bool &SizeIsNegative,
    llvm::APSInt &Oversized);

void checkDLLAttributeRedeclaration(Sema &S, NamedDecl *OldDecl,
                                    NamedDecl *NewDecl, bool IsSpecialization,
                                    bool IsDefinition);

bool isFunctionDefinitionDiscarded(Sema &S, FunctionDecl *FD);

bool shouldConsiderLinkage(const VarDecl *VD);
bool shouldConsiderLinkage(const FunctionDecl *FD);

bool hasParsedAttr(Scope *S, const Declarator &PD, ParsedAttr::Kind Kind);

void copyAllocSizeAttrFromTypedefToDecl(Sema &S, Decl *D,
                                        const TypedefType *TT);

StorageClass getFunctionStorageClass(Sema &SemaRef, Declarator &D);

FunctionDecl *createNewFunctionDecl(Sema &SemaRef, Declarator &D,
                                    DeclContext *DC, QualType &R,
                                    TypeSourceInfo *TInfo, StorageClass SC);

bool checkForConflictWithNonVisibleExternC(Sema &S, const VarDecl *ND,
                                           LookupResult &Previous);
bool checkForConflictWithNonVisibleExternC(Sema &S, const FunctionDecl *ND,
                                           LookupResult &Previous);

void emitReadOnlyPlacementAttrWarning(Sema &S, const VarDecl *VD);

bool isRepresentableIntegerValue(TreeContext &Context, llvm::APSInt &Value,
                                 QualType T);

QualType getNextLargerIntegralType(TreeContext &Context, QualType T);

} // namespace neverc

#endif // NEVERC_LIB_ANALYZE_DECL_SEMADECLUTILS_H
