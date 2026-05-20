#ifndef NEVERC_ANALYZE_SCOPE_H
#define NEVERC_ANALYZE_SCOPE_H

#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Tree/Decl/Decl.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <optional>

namespace llvm {

class raw_ostream;

} // namespace llvm

namespace neverc {

class Decl;
class DeclContext;
class VarDecl;

class Scope {
public:
  enum ScopeFlags {
    /// This indicates that the scope corresponds to a function, which
    /// means that labels are set here.
    FnScope = 0x01,

    /// This is a while, do, switch, for, etc that can have break
    /// statements embedded into it.
    BreakScope = 0x02,

    /// This is a while, do, for, which can have continue statements
    /// embedded into it.
    ContinueScope = 0x04,

    /// This is a scope that can contain a declaration.  Some scopes
    /// just contain loop constructs but don't contain decls.
    DeclScope = 0x08,

    /// The controlling scope in a if/switch/while/for statement.
    ControlScope = 0x10,

    /// The scope of a struct/union definition.
    RecordScope = 0x20,

    /// This is a scope that corresponds to the
    /// parameters within a function prototype.
    FunctionPrototypeScope = 0x100,

    /// This is a scope that corresponds to the parameters within
    /// a function prototype for a function declaration (as opposed to any
    /// other kind of function declarator). Always has FunctionPrototypeScope
    /// set as well.
    FunctionDeclarationScope = 0x200,

    /// This is a scope that corresponds to a switch statement.
    SwitchScope = 0x1000,

    /// This scope corresponds to an enum.
    EnumScope = 0x40000,

    /// This scope corresponds to an SEH try.
    SEHTryScope = 0x80000,

    /// This scope corresponds to an SEH except.
    SEHExceptScope = 0x100000,

    /// We are currently in the filter expression of an SEH except block.
    SEHFilterScope = 0x200000,

    /// This is a compound statement scope.
    CompoundStmtScope = 0x400000,
  };

private:
  Scope *AnyParent;

  unsigned Flags;

  unsigned short Depth;

  unsigned short MSLastManglingNumber;

  unsigned short MSCurManglingNumber;

  unsigned short PrototypeDepth;

  unsigned short PrototypeIndex;

  Scope *FnParent;
  Scope *MSLastManglingParent;

  Scope *BreakParent, *ContinueParent;

  using DeclSetTy = llvm::SmallPtrSet<Decl *, 16>;
  DeclSetTy DeclsInScope;

  DeclContext *Entity;

  DiagnosticErrorTrap ErrorTrap;

  std::optional<VarDecl *> NRVO;

  llvm::SmallPtrSet<VarDecl *, 8> ReturnSlots;

  void setFlags(Scope *Parent, unsigned F);

public:
  Scope(Scope *Parent, unsigned ScopeFlags, DiagnosticsEngine &Diag)
      : ErrorTrap(Diag) {
    Init(Parent, ScopeFlags);
  }

  unsigned getFlags() const { return Flags; }

  void setFlags(unsigned F) { setFlags(getParent(), F); }

  const Scope *getParent() const { return AnyParent; }
  Scope *getParent() { return AnyParent; }

  const Scope *getFnParent() const { return FnParent; }
  Scope *getFnParent() { return FnParent; }

  const Scope *getMSLastManglingParent() const { return MSLastManglingParent; }
  Scope *getMSLastManglingParent() { return MSLastManglingParent; }

  Scope *getContinueParent() { return ContinueParent; }

  const Scope *getContinueParent() const {
    return const_cast<Scope *>(this)->getContinueParent();
  }

  Scope *getBreakParent() { return BreakParent; }
  const Scope *getBreakParent() const {
    return const_cast<Scope *>(this)->getBreakParent();
  }

  unsigned getDepth() const { return Depth; }

  unsigned getFunctionPrototypeDepth() const { return PrototypeDepth; }

  unsigned getNextFunctionPrototypeIndex() {
    assert(isFunctionPrototypeScope());
    return PrototypeIndex++;
  }

  using decl_range = llvm::iterator_range<DeclSetTy::iterator>;

  decl_range decls() const {
    return decl_range(DeclsInScope.begin(), DeclsInScope.end());
  }

  bool decl_empty() const { return DeclsInScope.empty(); }

  void AddDecl(Decl *D) {
    DeclsInScope.insert(D);
    if (LLVM_UNLIKELY(FnParent != nullptr))
      if (auto *VD = dyn_cast<VarDecl>(D))
        if (!isa<ParmVarDecl>(VD))
          ReturnSlots.insert(VD);
  }

  void RemoveDecl(Decl *D) { DeclsInScope.erase(D); }

  void incrementMSManglingNumber() {
    if (Scope *MSLMP = getMSLastManglingParent()) {
      MSLMP->MSLastManglingNumber += 1;
      MSCurManglingNumber += 1;
    }
  }

  void decrementMSManglingNumber() {
    if (Scope *MSLMP = getMSLastManglingParent()) {
      MSLMP->MSLastManglingNumber -= 1;
      MSCurManglingNumber -= 1;
    }
  }

  unsigned getMSLastManglingNumber() const {
    if (const Scope *MSLMP = getMSLastManglingParent())
      return MSLMP->MSLastManglingNumber;
    return 1;
  }

  unsigned getMSCurManglingNumber() const { return MSCurManglingNumber; }

  bool isDeclScope(const Decl *D) const { return DeclsInScope.contains(D); }

  DeclContext *getEntity() const { return Entity; }

  DeclContext *getLookupEntity() const { return Entity; }

  void setEntity(DeclContext *E) { Entity = E; }
  void setLookupEntity(DeclContext *E) { Entity = E; }

  bool hasUnrecoverableErrorOccurred() const {
    return ErrorTrap.hasUnrecoverableErrorOccurred();
  }

  bool isFunctionScope() const { return getFlags() & Scope::FnScope; }

  bool isRecordScope() const { return getFlags() & Scope::RecordScope; }

  bool isFunctionPrototypeScope() const {
    return getFlags() & Scope::FunctionPrototypeScope;
  }

  bool isFunctionDeclarationScope() const {
    return getFlags() & Scope::FunctionDeclarationScope;
  }

  bool isSwitchScope() const {
    for (const Scope *S = this; S; S = S->getParent()) {
      if (S->getFlags() & Scope::SwitchScope)
        return true;
      else if (S->getFlags() & (Scope::FnScope | Scope::RecordScope |
                                Scope::FunctionPrototypeScope))
        return false;
    }
    return false;
  }

  bool isContinueScope() const {
    return getFlags() & ScopeFlags::ContinueScope;
  }

  bool isSEHTryScope() const { return getFlags() & Scope::SEHTryScope; }

  bool isSEHExceptScope() const { return getFlags() & Scope::SEHExceptScope; }

  bool isCompoundStmtScope() const {
    return getFlags() & Scope::CompoundStmtScope;
  }

  bool isControlScope() const { return getFlags() & Scope::ControlScope; }

  bool Contains(const Scope &rhs) const { return Depth < rhs.Depth; }

  bool containedInPrototypeScope() const;

  void updateNRVOCandidate(VarDecl *VD);

  void applyNRVO();

  void Init(Scope *parent, unsigned flags);

  void AddFlags(unsigned Flags);

  void dumpImpl(llvm::raw_ostream &OS) const;
  void dump() const;
};

} // namespace neverc

#endif // NEVERC_ANALYZE_SCOPE_H
