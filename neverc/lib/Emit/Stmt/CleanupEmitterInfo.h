#ifndef NEVERC_LIB_CODEGEN_STMT_CGCLEANUP_H
#define NEVERC_LIB_CODEGEN_STMT_CGCLEANUP_H

#include "Stmt/EHScopeStack.h"

#include "Core/Address.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"

namespace llvm {
class BasicBlock;
class ConstantInt;
} // namespace llvm

namespace neverc {
class FunctionDecl;
namespace Emit {
class ModuleEmitter;
class FunctionEmitter;

struct CatchTypeInfo {
  llvm::Constant *RTTI;
  unsigned Flags;
};

class EHScope {
  llvm::BasicBlock *CachedLandingPad;
  llvm::BasicBlock *CachedEHDispatchBlock;

  EHScopeStack::stable_iterator EnclosingEHScope;

  class CommonBitFields {
    friend class EHScope;
    unsigned Kind : 3;
  };
  enum { NumCommonBits = 3 };

protected:
  class CatchBitFields {
    friend class EHCatchScope;
    unsigned : NumCommonBits;

    unsigned NumHandlers : 32 - NumCommonBits;
  };

  class CleanupBitFields {
    friend class EHCleanupScope;
    unsigned : NumCommonBits;

    /// Whether this cleanup needs to be run along normal edges.
    unsigned IsNormalCleanup : 1;

    /// Whether this cleanup needs to be run along exception edges.
    unsigned IsEHCleanup : 1;

    /// Whether this cleanup is currently active.
    unsigned IsActive : 1;

    /// Whether this cleanup is a lifetime marker
    unsigned IsLifetimeMarker : 1;

    /// Whether the normal cleanup should test the activation flag.
    unsigned TestFlagInNormalCleanup : 1;

    /// Whether the EH cleanup should test the activation flag.
    unsigned TestFlagInEHCleanup : 1;

    /// The amount of extra storage needed by the Cleanup.
    /// Always a multiple of the scope-stack alignment.
    unsigned CleanupSize : 12;
  };

  union {
    CommonBitFields CommonBits;
    CatchBitFields CatchBits;
    CleanupBitFields CleanupBits;
  };

public:
  enum Kind { Cleanup, Catch, Terminate };

  EHScope(Kind kind, EHScopeStack::stable_iterator enclosingEHScope)
      : CachedLandingPad(nullptr), CachedEHDispatchBlock(nullptr),
        EnclosingEHScope(enclosingEHScope) {
    CommonBits.Kind = kind;
  }

  Kind getKind() const { return static_cast<Kind>(CommonBits.Kind); }

  llvm::BasicBlock *getCachedLandingPad() const { return CachedLandingPad; }

  void setCachedLandingPad(llvm::BasicBlock *block) {
    CachedLandingPad = block;
  }

  llvm::BasicBlock *getCachedEHDispatchBlock() const {
    return CachedEHDispatchBlock;
  }

  void setCachedEHDispatchBlock(llvm::BasicBlock *block) {
    CachedEHDispatchBlock = block;
  }

  bool hasEHBranches() const {
    if (llvm::BasicBlock *block = getCachedEHDispatchBlock())
      return !block->use_empty();
    return false;
  }

  EHScopeStack::stable_iterator getEnclosingEHScope() const {
    return EnclosingEHScope;
  }
};

class EHCatchScope : public EHScope {
  // In effect, we have a flexible array member
  //   Handler Handlers[0];
  // but we use pointer arithmetic instead.

public:
  struct Handler {
    /// A type info value, or null for a catch-all.
    CatchTypeInfo Type;

    /// The catch handler for this type.
    llvm::BasicBlock *Block;

    bool isCatchAll() const { return Type.RTTI == nullptr; }
  };

private:
  friend class EHScopeStack;

  Handler *getHandlers() { return reinterpret_cast<Handler *>(this + 1); }

  const Handler *getHandlers() const {
    return reinterpret_cast<const Handler *>(this + 1);
  }

public:
  static size_t getSizeForNumHandlers(unsigned N) {
    return sizeof(EHCatchScope) + N * sizeof(Handler);
  }

  EHCatchScope(unsigned numHandlers,
               EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(Catch, enclosingEHScope) {
    CatchBits.NumHandlers = numHandlers;
    assert(CatchBits.NumHandlers == numHandlers && "NumHandlers overflow?");
  }

  unsigned getNumHandlers() const { return CatchBits.NumHandlers; }

  void setCatchAllHandler(unsigned I, llvm::BasicBlock *Block) {
    setHandler(I, CatchTypeInfo{nullptr, 0}, Block);
  }

  void setHandler(unsigned I, llvm::Constant *Type, llvm::BasicBlock *Block) {
    assert(I < getNumHandlers());
    getHandlers()[I].Type = CatchTypeInfo{Type, 0};
    getHandlers()[I].Block = Block;
  }

  void setHandler(unsigned I, CatchTypeInfo Type, llvm::BasicBlock *Block) {
    assert(I < getNumHandlers());
    getHandlers()[I].Type = Type;
    getHandlers()[I].Block = Block;
  }

  const Handler &getHandler(unsigned I) const {
    assert(I < getNumHandlers());
    return getHandlers()[I];
  }

  // Clear all handler blocks.
  void clearHandlerBlocks() {
    for (unsigned I = 0, N = getNumHandlers(); I != N; ++I)
      delete getHandler(I).Block;
  }

  typedef const Handler *iterator;
  iterator begin() const { return getHandlers(); }
  iterator end() const { return getHandlers() + getNumHandlers(); }

  static bool classof(const EHScope *Scope) {
    return Scope->getKind() == Catch;
  }
};

class alignas(8) EHCleanupScope : public EHScope {
  EHScopeStack::stable_iterator EnclosingNormal;

  EHScopeStack::stable_iterator EnclosingEH;

  llvm::BasicBlock *NormalBlock;

  Address ActiveFlag;

  struct ExtInfo {
    /// The destinations of normal branch-afters and branch-throughs.
    llvm::SmallPtrSet<llvm::BasicBlock *, 4> Branches;

    /// Normal branch-afters.
    llvm::SmallVector<std::pair<llvm::BasicBlock *, llvm::ConstantInt *>, 4>
        BranchAfters;
  };
  mutable struct ExtInfo *ExtInfo;

  unsigned FixupDepth;

  struct ExtInfo &getExtInfo() {
    if (!ExtInfo)
      ExtInfo = new struct ExtInfo();
    return *ExtInfo;
  }

  const struct ExtInfo &getExtInfo() const {
    if (!ExtInfo)
      ExtInfo = new struct ExtInfo();
    return *ExtInfo;
  }

public:
  static size_t getSizeForCleanupSize(size_t Size) {
    return sizeof(EHCleanupScope) + Size;
  }

  size_t getAllocatedSize() const {
    return sizeof(EHCleanupScope) + CleanupBits.CleanupSize;
  }

  EHCleanupScope(bool isNormal, bool isEH, unsigned cleanupSize,
                 unsigned fixupDepth,
                 EHScopeStack::stable_iterator enclosingNormal,
                 EHScopeStack::stable_iterator enclosingEH)
      : EHScope(EHScope::Cleanup, enclosingEH),
        EnclosingNormal(enclosingNormal), NormalBlock(nullptr),
        ActiveFlag(Address::invalid()), ExtInfo(nullptr),
        FixupDepth(fixupDepth) {
    CleanupBits.IsNormalCleanup = isNormal;
    CleanupBits.IsEHCleanup = isEH;
    CleanupBits.IsActive = true;
    CleanupBits.IsLifetimeMarker = false;
    CleanupBits.TestFlagInNormalCleanup = false;
    CleanupBits.TestFlagInEHCleanup = false;
    CleanupBits.CleanupSize = cleanupSize;

    assert(CleanupBits.CleanupSize == cleanupSize && "cleanup size overflow");
  }

  void Destroy() { delete ExtInfo; }
  // Objects of EHCleanupScope are not destructed. Use Destroy().
  ~EHCleanupScope() = delete;

  bool isNormalCleanup() const { return CleanupBits.IsNormalCleanup; }
  llvm::BasicBlock *getNormalBlock() const { return NormalBlock; }
  void setNormalBlock(llvm::BasicBlock *BB) { NormalBlock = BB; }

  bool isEHCleanup() const { return CleanupBits.IsEHCleanup; }

  bool isActive() const { return CleanupBits.IsActive; }
  void setActive(bool A) { CleanupBits.IsActive = A; }

  bool isLifetimeMarker() const { return CleanupBits.IsLifetimeMarker; }
  void setLifetimeMarker() { CleanupBits.IsLifetimeMarker = true; }

  bool hasActiveFlag() const { return ActiveFlag.isValid(); }
  Address getActiveFlag() const { return ActiveFlag; }
  void setActiveFlag(Address Var) {
    assert(Var.getAlignment().isOne());
    ActiveFlag = Var;
  }

  void setTestFlagInNormalCleanup() {
    CleanupBits.TestFlagInNormalCleanup = true;
  }
  bool shouldTestFlagInNormalCleanup() const {
    return CleanupBits.TestFlagInNormalCleanup;
  }

  void setTestFlagInEHCleanup() { CleanupBits.TestFlagInEHCleanup = true; }
  bool shouldTestFlagInEHCleanup() const {
    return CleanupBits.TestFlagInEHCleanup;
  }

  unsigned getFixupDepth() const { return FixupDepth; }
  EHScopeStack::stable_iterator getEnclosingNormalCleanup() const {
    return EnclosingNormal;
  }

  size_t getCleanupSize() const { return CleanupBits.CleanupSize; }
  void *getCleanupBuffer() { return this + 1; }

  EHScopeStack::Cleanup *getCleanup() {
    return reinterpret_cast<EHScopeStack::Cleanup *>(getCleanupBuffer());
  }

  bool hasBranches() const { return ExtInfo && !ExtInfo->Branches.empty(); }

  void addBranchAfter(llvm::ConstantInt *Index, llvm::BasicBlock *Block) {
    struct ExtInfo &ExtInfo = getExtInfo();
    if (ExtInfo.Branches.insert(Block).second)
      ExtInfo.BranchAfters.push_back(std::make_pair(Block, Index));
  }

  unsigned getNumBranchAfters() const {
    return ExtInfo ? ExtInfo->BranchAfters.size() : 0;
  }

  llvm::BasicBlock *getBranchAfterBlock(unsigned I) const {
    assert(I < getNumBranchAfters());
    return ExtInfo->BranchAfters[I].first;
  }

  llvm::ConstantInt *getBranchAfterIndex(unsigned I) const {
    assert(I < getNumBranchAfters());
    return ExtInfo->BranchAfters[I].second;
  }

  bool addBranchThrough(llvm::BasicBlock *Block) {
    return getExtInfo().Branches.insert(Block).second;
  }

  bool hasBranchThroughs() const {
    if (!ExtInfo)
      return false;
    return (ExtInfo->BranchAfters.size() != ExtInfo->Branches.size());
  }

  static bool classof(const EHScope *Scope) {
    return (Scope->getKind() == Cleanup);
  }
};
// NOTE: there's a bunch of different data classes tacked on after an
// EHCleanupScope. It is asserted (in EHScopeStack::pushCleanup*) that
// they don't require greater alignment than ScopeStackAlignment. So,
// EHCleanupScope ought to have alignment equal to that -- not more
// (would be misaligned by the stack allocator), and not less (would
// break the appended classes).
static_assert(alignof(EHCleanupScope) == EHScopeStack::ScopeStackAlignment,
              "EHCleanupScope expected alignment");

class EHTerminateScope : public EHScope {
public:
  EHTerminateScope(EHScopeStack::stable_iterator enclosingEHScope)
      : EHScope(Terminate, enclosingEHScope) {}
  static size_t getSize() { return sizeof(EHTerminateScope); }

  static bool classof(const EHScope *scope) {
    return scope->getKind() == Terminate;
  }
};

class EHScopeStack::iterator {
  char *Ptr;

  friend class EHScopeStack;
  explicit iterator(char *Ptr) : Ptr(Ptr) {}

public:
  iterator() : Ptr(nullptr) {}

  EHScope *get() const { return reinterpret_cast<EHScope *>(Ptr); }

  EHScope *operator->() const { return get(); }
  EHScope &operator*() const { return *get(); }

  iterator &operator++() {
    size_t Size;
    switch (get()->getKind()) {
    case EHScope::Catch:
      Size = EHCatchScope::getSizeForNumHandlers(
          static_cast<const EHCatchScope *>(get())->getNumHandlers());
      break;

    case EHScope::Cleanup:
      Size = static_cast<const EHCleanupScope *>(get())->getAllocatedSize();
      break;

    case EHScope::Terminate:
      Size = EHTerminateScope::getSize();
      break;
    }
    Ptr += llvm::alignTo(Size, ScopeStackAlignment);
    return *this;
  }

  iterator next() {
    iterator copy = *this;
    ++copy;
    return copy;
  }

  iterator operator++(int) {
    iterator copy = *this;
    operator++();
    return copy;
  }

  bool encloses(iterator other) const { return Ptr >= other.Ptr; }
  bool strictlyEncloses(iterator other) const { return Ptr > other.Ptr; }

  bool operator==(iterator other) const { return Ptr == other.Ptr; }
  bool operator!=(iterator other) const { return Ptr != other.Ptr; }
};

inline EHScopeStack::iterator EHScopeStack::begin() const {
  return iterator(StartOfData);
}

inline EHScopeStack::iterator EHScopeStack::end() const {
  return iterator(EndOfBuffer);
}

inline void EHScopeStack::popCatch() {
  assert(!empty() && "popping exception stack when not empty");

  EHCatchScope &scope = llvm::cast<EHCatchScope>(*begin());
  InnermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHCatchScope::getSizeForNumHandlers(scope.getNumHandlers()));
}

inline void EHScopeStack::popTerminate() {
  assert(!empty() && "popping exception stack when not empty");

  EHTerminateScope &scope = llvm::cast<EHTerminateScope>(*begin());
  InnermostEHScope = scope.getEnclosingEHScope();
  deallocate(EHTerminateScope::getSize());
}

inline EHScopeStack::iterator EHScopeStack::find(stable_iterator sp) const {
  assert(sp.isValid() && "finding invalid savepoint");
  assert(sp.Size <= stable_begin().Size && "finding savepoint after pop");
  return iterator(EndOfBuffer - sp.Size);
}

inline EHScopeStack::stable_iterator
EHScopeStack::stabilize(iterator ir) const {
  assert(StartOfData <= ir.Ptr && ir.Ptr <= EndOfBuffer);
  return stable_iterator(EndOfBuffer - ir.Ptr);
}

struct EHPersonality {
  const char *PersonalityFn;

  static const EHPersonality &get(ModuleEmitter &ME, const FunctionDecl *FD);
  static const EHPersonality &get(FunctionEmitter &FE);

  static const EHPersonality GNU_C;
  static const EHPersonality GNU_C_SEH;
  static const EHPersonality MSVC_C_specific_handler;

  bool usesFuncletPads() const { return isMSVCPersonality(); }

  bool isMSVCPersonality() const { return this == &MSVC_C_specific_handler; }
};
} // namespace Emit
} // namespace neverc

#endif
