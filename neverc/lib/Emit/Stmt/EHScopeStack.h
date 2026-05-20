#ifndef NEVERC_LIB_EMIT_STMT_EHSCOPESTACK_H
#define NEVERC_LIB_EMIT_STMT_EHSCOPESTACK_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

namespace neverc {
namespace Emit {

class FunctionEmitter;

struct BranchFixup {
  llvm::BasicBlock *OptimisticBranchBlock;

  llvm::BasicBlock *Destination;

  unsigned DestinationIndex;

  llvm::BranchInst *InitialBranch;
};

template <class T> struct InvariantValue {
  typedef T type;
  typedef T saved_type;
  static bool needsSaving(type value) { return false; }
  static saved_type save(FunctionEmitter &FE, type value) { return value; }
  static type restore(FunctionEmitter &FE, saved_type value) { return value; }
};

template <class T> struct DominatingValue : InvariantValue<T> {};

template <class T, bool mightBeInstruction =
                       std::is_base_of<llvm::Value, T>::value &&
                       !std::is_base_of<llvm::Constant, T>::value &&
                       !std::is_base_of<llvm::BasicBlock, T>::value>
struct DominatingPointer;
template <class T> struct DominatingPointer<T, false> : InvariantValue<T *> {};
// template <class T> struct DominatingPointer<T,true> at end of file

template <class T> struct DominatingValue<T *> : DominatingPointer<T> {};

enum CleanupKind : unsigned {
  EHCleanup = 0x1,

  NormalCleanup = 0x2,

  NormalAndEHCleanup = EHCleanup | NormalCleanup,

  LifetimeMarker = 0x8,
  NormalEHLifetimeMarker = LifetimeMarker | NormalAndEHCleanup,
};

class EHScopeStack {
public:
  /* Should switch to alignof(uint64_t) instead of 8, when EHCleanupScope can */
  enum { ScopeStackAlignment = 8 };

  class stable_iterator {
    friend class EHScopeStack;

    /// Offset from StartOfData to EndOfBuffer.
    ptrdiff_t Size;

    stable_iterator(ptrdiff_t Size) : Size(Size) {}

  public:
    static stable_iterator invalid() { return stable_iterator(-1); }
    stable_iterator() : Size(-1) {}

    bool isValid() const { return Size >= 0; }

    /// Returns true if this scope encloses I.
    /// Returns false if I is invalid.
    /// This scope must be valid.
    bool encloses(stable_iterator I) const { return Size <= I.Size; }

    /// Returns true if this scope strictly encloses I: that is,
    /// if it encloses I and is not I.
    /// Returns false is I is invalid.
    /// This scope must be valid.
    bool strictlyEncloses(stable_iterator I) const { return Size < I.Size; }

    friend bool operator==(stable_iterator A, stable_iterator B) {
      return A.Size == B.Size;
    }
    friend bool operator!=(stable_iterator A, stable_iterator B) {
      return A.Size != B.Size;
    }
  };

  class Cleanup {
    // Anchor the construction vtable.
    virtual void anchor();

  protected:
    ~Cleanup() = default;

  public:
    Cleanup(const Cleanup &) = default;
    Cleanup(Cleanup &&) {}

    // The copy and move assignment operator is defined as deleted pending
    // further motivation.
    Cleanup &operator=(const Cleanup &) = delete;
    Cleanup &operator=(Cleanup &&) = delete;

    Cleanup() = default;

    virtual bool isRedundantBeforeReturn() { return false; }

    /// Generation flags.
    class Flags {
      enum {
        F_IsForEH = 0x1,
        F_IsNormalCleanupKind = 0x2,
        F_IsEHCleanupKind = 0x4,
        F_HasExitSwitch = 0x8,
      };
      unsigned flags = 0;

    public:
      Flags() = default;

      /// isForEH - true if the current emission is for an EH cleanup.
      bool isForEHCleanup() const { return flags & F_IsForEH; }
      bool isForNormalCleanup() const { return !isForEHCleanup(); }
      void setIsForEHCleanup() { flags |= F_IsForEH; }

      bool isNormalCleanupKind() const { return flags & F_IsNormalCleanupKind; }
      void setIsNormalCleanupKind() { flags |= F_IsNormalCleanupKind; }

      /// isEHCleanupKind - true if the cleanup was pushed as an EH
      /// cleanup.
      bool isEHCleanupKind() const { return flags & F_IsEHCleanupKind; }
      void setIsEHCleanupKind() { flags |= F_IsEHCleanupKind; }

      bool hasExitSwitch() const { return flags & F_HasExitSwitch; }
      void setHasExitSwitch() { flags |= F_HasExitSwitch; }
    };

    /// Emit the cleanup.  For normal cleanups, this is run in the
    /// same EH context as when the cleanup was pushed, i.e. the
    /// immediately-enclosing context of the cleanup scope.  For
    /// EH cleanups, this is run in a terminate context.
    // \param flags cleanup kind.
    virtual void Emit(FunctionEmitter &FE, Flags flags) = 0;
  };

  template <class T, class... As>
  class ConditionalCleanup final : public Cleanup {
    typedef std::tuple<typename DominatingValue<As>::saved_type...> SavedTuple;
    SavedTuple Saved;

    template <std::size_t... Is>
    T restore(FunctionEmitter &FE, std::index_sequence<Is...>) {
      // It's important that the restores are emitted in order. The braced init
      // list guarantees that.
      return T{DominatingValue<As>::restore(FE, std::get<Is>(Saved))...};
    }

    void Emit(FunctionEmitter &FE, Flags flags) override {
      restore(FE, std::index_sequence_for<As...>()).Emit(FE, flags);
    }

  public:
    ConditionalCleanup(typename DominatingValue<As>::saved_type... A)
        : Saved(A...) {}

    ConditionalCleanup(SavedTuple Tuple) : Saved(std::move(Tuple)) {}
  };

private:
  // The implementation for this class is in ExceptionEmitter.cpp and
  // ExceptionEmitter.cpp; the definition is here because it's used as a
  // member of FunctionEmitter.

  char *StartOfBuffer;

  char *EndOfBuffer;

  char *StartOfData;

  stable_iterator InnermostNormalCleanup;

  stable_iterator InnermostEHScope;

  FunctionEmitter *FE;

  llvm::SmallVector<BranchFixup, 8> BranchFixups;

  char *allocate(size_t Size);
  void deallocate(size_t Size);

  void *pushCleanup(CleanupKind K, size_t DataSize);

public:
  EHScopeStack()
      : StartOfBuffer(nullptr), EndOfBuffer(nullptr), StartOfData(nullptr),
        InnermostNormalCleanup(stable_end()), InnermostEHScope(stable_end()),
        FE(nullptr) {}
  ~EHScopeStack() { delete[] StartOfBuffer; }

  EHScopeStack(const EHScopeStack &) = delete;
  EHScopeStack &operator=(const EHScopeStack &) = delete;

  template <class T, class... As> void pushCleanup(CleanupKind Kind, As... A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T));
    Cleanup *Obj = new (Buffer) T(A...);
    (void)Obj;
  }

  template <class T, class... As>
  void pushCleanupTuple(CleanupKind Kind, std::tuple<As...> A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T));
    Cleanup *Obj = new (Buffer) T(std::move(A));
    (void)Obj;
  }

  // Feel free to add more variants of the following:

  template <class T, class... As>
  T *pushCleanupWithExtra(CleanupKind Kind, size_t N, As... A) {
    static_assert(alignof(T) <= ScopeStackAlignment,
                  "Cleanup's alignment is too large.");
    void *Buffer = pushCleanup(Kind, sizeof(T) + T::getExtraSize(N));
    return new (Buffer) T(N, A...);
  }

  void pushCopyOfCleanup(CleanupKind Kind, const void *Cleanup, size_t Size) {
    void *Buffer = pushCleanup(Kind, Size);
    std::memcpy(Buffer, Cleanup, Size);
  }

  void setFunctionEmitter(FunctionEmitter *InFE) { FE = InFE; }

  void popCleanup();

  class EHCatchScope *pushCatch(unsigned NumHandlers);

  void popCatch();

  void pushTerminate();

  void popTerminate();

  // Returns true iff the current scope is either empty or contains only
  // lifetime markers, i.e. no real cleanup code
  bool containsOnlyLifetimeMarkers(stable_iterator Old) const;

  bool empty() const { return StartOfData == EndOfBuffer; }

  bool requiresLandingPad() const;

  bool hasNormalCleanups() const {
    return InnermostNormalCleanup != stable_end();
  }

  stable_iterator getInnermostNormalCleanup() const {
    return InnermostNormalCleanup;
  }
  stable_iterator getInnermostActiveNormalCleanup() const;

  stable_iterator getInnermostEHScope() const { return InnermostEHScope; }

  class iterator;

  iterator begin() const;

  iterator end() const;

  stable_iterator stable_begin() const {
    return stable_iterator(EndOfBuffer - StartOfData);
  }

  static stable_iterator stable_end() { return stable_iterator(0); }

  stable_iterator stabilize(iterator it) const;

  iterator find(stable_iterator save) const;

  BranchFixup &addBranchFixup() {
    assert(hasNormalCleanups() && "adding fixup in scope without cleanups");
    BranchFixups.push_back(BranchFixup());
    return BranchFixups.back();
  }

  unsigned getNumBranchFixups() const { return BranchFixups.size(); }
  BranchFixup &getBranchFixup(unsigned I) {
    assert(I < getNumBranchFixups());
    return BranchFixups[I];
  }

  void popNullFixups();

  void clearFixups() { BranchFixups.clear(); }
};

} // namespace Emit
} // namespace neverc

#endif
