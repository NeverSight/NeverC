#ifndef NEVERC_ANALYZE_LOOKUP_H
#define NEVERC_ANALYZE_LOOKUP_H

#include "neverc/Analyze/SemaNameLookupKinds.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Decl/UnresolvedSet.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <utility>

namespace neverc {

class Sema;

using llvm::dyn_cast;
using llvm::isa;

class LookupResult {
public:
  enum LookupResultKind {
    /// No entity found met the criteria.
    NotFound = 0,

    /// Name lookup found a single declaration that met the
    /// criteria.  getFoundDecl() will return this declaration.
    Found,

    /// Name lookup found a set of overloaded functions that
    /// met the criteria.
    FoundOverloaded,

    /// Multiple visible declarations for the same name could not be reduced
    /// to a single result (e.g. tag hidden by non-tag, or mixed kinds).
    Ambiguous
  };

  enum TemporaryToken { Temporary };

  using iterator = UnresolvedSetImpl::iterator;

  LookupResult(Sema &SemaRef, const DeclarationNameInfo &NameInfo,
               neverc::ResolveNameKind LookupKind,
               neverc::RedeclarationKind Redecl = neverc::NotForRedeclaration)
      : SemaPtr(&SemaRef), NameInfo(NameInfo), LookupKind(LookupKind),
        Redecl(Redecl != neverc::NotForRedeclaration),
        ExternalRedecl(Redecl == neverc::ForExternalRedeclaration),
        DiagnoseAmbiguous(Redecl == neverc::NotForRedeclaration) {
    configure();
  }

  LookupResult(Sema &SemaRef, DeclarationName Name, SourceLocation NameLoc,
               neverc::ResolveNameKind LookupKind,
               neverc::RedeclarationKind Redecl = neverc::NotForRedeclaration)
      : SemaPtr(&SemaRef), NameInfo(Name, NameLoc), LookupKind(LookupKind),
        Redecl(Redecl != neverc::NotForRedeclaration),
        ExternalRedecl(Redecl == neverc::ForExternalRedeclaration),
        DiagnoseAmbiguous(Redecl == neverc::NotForRedeclaration) {
    configure();
  }

  LookupResult(TemporaryToken _, const LookupResult &Other)
      : SemaPtr(Other.SemaPtr), NameInfo(Other.NameInfo),
        LookupKind(Other.LookupKind), IDNS(Other.IDNS), Redecl(Other.Redecl),
        ExternalRedecl(Other.ExternalRedecl), HideTags(Other.HideTags),
        AllowHidden(Other.AllowHidden) {}

  LookupResult(const LookupResult &) = delete;
  LookupResult &operator=(const LookupResult &) = delete;

  LookupResult(LookupResult &&Other)
      : ResultKind(std::move(Other.ResultKind)), Decls(std::move(Other.Decls)),
        SemaPtr(std::move(Other.SemaPtr)), NameInfo(std::move(Other.NameInfo)),
        NameContextRange(std::move(Other.NameContextRange)),
        LookupKind(std::move(Other.LookupKind)), IDNS(std::move(Other.IDNS)),
        Redecl(std::move(Other.Redecl)),
        ExternalRedecl(std::move(Other.ExternalRedecl)),
        HideTags(std::move(Other.HideTags)),
        DiagnoseAmbiguous(std::move(Other.DiagnoseAmbiguous)),
        AllowHidden(std::move(Other.AllowHidden)),
        Shadowed(std::move(Other.Shadowed)) {
    Other.DiagnoseAmbiguous = false;
  }

  LookupResult &operator=(LookupResult &&Other) {
    ResultKind = std::move(Other.ResultKind);
    Decls = std::move(Other.Decls);
    SemaPtr = std::move(Other.SemaPtr);
    NameInfo = std::move(Other.NameInfo);
    NameContextRange = std::move(Other.NameContextRange);
    LookupKind = std::move(Other.LookupKind);
    IDNS = std::move(Other.IDNS);
    Redecl = std::move(Other.Redecl);
    ExternalRedecl = std::move(Other.ExternalRedecl);
    HideTags = std::move(Other.HideTags);
    DiagnoseAmbiguous = std::move(Other.DiagnoseAmbiguous);
    AllowHidden = std::move(Other.AllowHidden);
    Shadowed = std::move(Other.Shadowed);
    Other.DiagnoseAmbiguous = false;
    return *this;
  }

  ~LookupResult();

  const DeclarationNameInfo &getLookupNameInfo() const { return NameInfo; }

  DeclarationName getLookupName() const { return NameInfo.getName(); }

  void setLookupName(DeclarationName Name) { NameInfo.setName(Name); }

  neverc::ResolveNameKind getLookupKind() const { return LookupKind; }

  bool isForRedeclaration() const { return Redecl; }

  bool isForExternalRedeclaration() const { return ExternalRedecl; }

  neverc::RedeclarationKind redeclarationKind() const {
    return ExternalRedecl ? neverc::ForExternalRedeclaration
           : Redecl       ? neverc::ForVisibleRedeclaration
                          : neverc::NotForRedeclaration;
  }

  void setAllowHidden(bool AH) { AllowHidden = AH; }

  bool isHiddenDeclarationVisible(NamedDecl *ND) const {
    return AllowHidden ||
           (isForExternalRedeclaration() && ND->isExternallyDeclarable());
  }

  void setHideTags(bool Hide) { HideTags = Hide; }

  bool isAmbiguous() const { return getResultKind() == Ambiguous; }

  bool isSingleResult() const { return getResultKind() == Found; }

  bool isOverloadedResult() const { return getResultKind() == FoundOverloaded; }

  LookupResultKind getResultKind() const {
    assert(checkDebugAssumptions());
    return ResultKind;
  }

  iterator begin() const { return iterator(Decls.begin()); }
  iterator end() const { return iterator(Decls.end()); }

  bool empty() const { return Decls.empty(); }

  static bool isVisible(Sema &SemaRef, NamedDecl *D);

  static bool isAvailableForLookup(Sema &SemaRef, NamedDecl *ND);

  NamedDecl *getAcceptableDecl(NamedDecl *D) const {
    if (!D->isInIdentifierNamespace(IDNS))
      return nullptr;

    if (isAvailableForLookup(getSema(), D) || isHiddenDeclarationVisible(D))
      return D;

    return getAcceptableDeclSlow(D);
  }

private:
  static bool isVisibleSlow(Sema &SemaRef, NamedDecl *D);
  NamedDecl *getAcceptableDeclSlow(NamedDecl *D) const;

public:
  unsigned getIdentifierNamespace() const { return IDNS; }

  void addDecl(NamedDecl *D) {
    Decls.addDecl(D);
    ResultKind = Found;
  }

  bool isShadowed() const { return Shadowed; }

  void setShadowed() { Shadowed = true; }

  void resolveKind();

  void resolveKindAfterFilter() {
    if (Decls.empty()) {
      ResultKind = NotFound;
    } else {
      ResultKind = Found;
      resolveKind();
    }
  }

  template <class DeclClass> DeclClass *getAsSingle() const {
    if (getResultKind() != Found)
      return nullptr;
    return dyn_cast<DeclClass>(getFoundDecl());
  }

  NamedDecl *getFoundDecl() const { return (*begin())->getUnderlyingDecl(); }

  NamedDecl *getRepresentativeDecl() const {
    assert(!Decls.empty() && "cannot get representative of empty set");
    return *begin();
  }

  LLVM_ATTRIBUTE_REINITIALIZES void clear() {
    ResultKind = NotFound;
    Decls.clear();
    Shadowed = false;
  }

  void clear(neverc::ResolveNameKind Kind) {
    clear();
    LookupKind = Kind;
    configure();
  }

  void setRedeclarationKind(neverc::RedeclarationKind RK) {
    Redecl = (RK != neverc::NotForRedeclaration);
    ExternalRedecl = (RK == neverc::ForExternalRedeclaration);
    configure();
  }

  void dump();

  void suppressDiagnostics() { DiagnoseAmbiguous = false; }

  void setContextRange(SourceRange SR) { NameContextRange = SR; }

  SourceRange getContextRange() const { return NameContextRange; }

  SourceLocation getNameLoc() const { return NameInfo.getLoc(); }

  Sema &getSema() const { return *SemaPtr; }

  class Filter {
    friend class LookupResult;

    LookupResult &Results;
    LookupResult::iterator I;
    bool Changed = false;
    bool CalledDone = false;

    Filter(LookupResult &Results) : Results(Results), I(Results.begin()) {}

  public:
    Filter(Filter &&F)
        : Results(F.Results), I(F.I), Changed(F.Changed),
          CalledDone(F.CalledDone) {
      F.CalledDone = true;
    }

    // The move assignment operator is defined as deleted pending
    // further motivation.
    Filter &operator=(Filter &&) = delete;

    // The copy constrcutor and copy assignment operator is defined as deleted
    // pending further motivation.
    Filter(const Filter &) = delete;
    Filter &operator=(const Filter &) = delete;

    ~Filter() {
      assert(CalledDone &&
             "LookupResult::Filter destroyed without done() call");
    }

    bool hasNext() const { return I != Results.end(); }

    NamedDecl *next() {
      assert(I != Results.end() && "next() called on empty filter");
      return *I++;
    }

    /// Erase the last element returned from this iterator.
    void erase() {
      Results.Decls.erase(--I);
      Changed = true;
    }

    void done() {
      assert(!CalledDone && "done() called twice");
      CalledDone = true;

      if (Changed)
        Results.resolveKindAfterFilter();
    }
  };

  Filter makeFilter() { return Filter(*this); }

  void setFindLocalExtern(bool FindLocalExtern) {
    if (FindLocalExtern)
      IDNS |= Decl::IDNS_LocalExtern;
    else
      IDNS &= ~Decl::IDNS_LocalExtern;
  }

private:
  void diagnoseAmbiguous();

  void configure();

  bool checkDebugAssumptions() const;

  // Results.
  LookupResultKind ResultKind = NotFound;
  UnresolvedSet<8> Decls;

  // Parameters.
  Sema *SemaPtr;
  DeclarationNameInfo NameInfo;
  SourceRange NameContextRange;
  neverc::ResolveNameKind LookupKind;
  unsigned IDNS = 0; // set by configure()

  bool Redecl;
  bool ExternalRedecl;

  bool HideTags = true;

  bool DiagnoseAmbiguous = false;

  bool AllowHidden = false;

  bool Shadowed = false;
};

} // namespace neverc

#endif // NEVERC_ANALYZE_LOOKUP_H
