#ifndef NEVERC_ANALYZE_OWNERSHIP_H
#define NEVERC_ANALYZE_OWNERSHIP_H

#include "neverc/Tree/Expr/Expr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>

//===----------------------------------------------------------------------===//
// OpaquePtr
//===----------------------------------------------------------------------===//

namespace neverc {

class Decl;
class Expr;
class QualType;
class Stmt;

template <class PtrTy> class OpaquePtr {
  void *Ptr = nullptr;

  explicit OpaquePtr(void *Ptr) : Ptr(Ptr) {}

  using Traits = llvm::PointerLikeTypeTraits<PtrTy>;

public:
  OpaquePtr(std::nullptr_t = nullptr) {}

  static OpaquePtr make(PtrTy P) {
    OpaquePtr OP;
    OP.set(P);
    return OP;
  }

  template <typename PointeeT> PointeeT *getPtrTo() const { return get(); }

  template <typename PtrT> PtrT getPtrAs() const { return get(); }

  PtrTy get() const { return Traits::getFromVoidPointer(Ptr); }

  void set(PtrTy P) { Ptr = Traits::getAsVoidPointer(P); }

  explicit operator bool() const { return Ptr != nullptr; }

  void *getAsOpaquePtr() const { return Ptr; }
  static OpaquePtr getFromOpaquePtr(void *P) { return OpaquePtr(P); }
};

template <class T> struct UnionOpaquePtr {
  void *Ptr;

  static UnionOpaquePtr make(OpaquePtr<T> P) {
    UnionOpaquePtr OP = {P.getAsOpaquePtr()};
    return OP;
  }

  OpaquePtr<T> get() const { return OpaquePtr<T>::getFromOpaquePtr(Ptr); }
  operator OpaquePtr<T>() const { return get(); }

  UnionOpaquePtr &operator=(OpaquePtr<T> P) {
    Ptr = P.getAsOpaquePtr();
    return *this;
  }
};

} // namespace neverc

namespace llvm {

template <class T> struct PointerLikeTypeTraits<neverc::OpaquePtr<T>> {
  static constexpr int NumLowBitsAvailable = 0;

  static inline void *getAsVoidPointer(neverc::OpaquePtr<T> P) {
    return P.getAsOpaquePtr();
  }

  static inline neverc::OpaquePtr<T> getFromVoidPointer(void *P) {
    return neverc::OpaquePtr<T>::getFromOpaquePtr(P);
  }
};

} // namespace llvm

namespace neverc {

class StreamingDiagnostic;

// Determines whether the low bit of the result pointer for the
// given UID is always zero. If so, ActionResult will use that bit
// for it's "invalid" flag.
template <class Ptr> struct IsResultPtrLowBitFree {
  static const bool value = false;
};

template <class PtrTy, bool Compress = IsResultPtrLowBitFree<PtrTy>::value>
class ActionResult {
  PtrTy Val = {};
  bool Invalid = false;

public:
  ActionResult(bool Invalid = false) : Val(PtrTy()), Invalid(Invalid) {}
  ActionResult(PtrTy Val) { *this = Val; }
  ActionResult(const DiagnosticBuilder &) : ActionResult(/*Invalid=*/true) {}

  // These two overloads prevent void* -> bool conversions.
  ActionResult(const void *) = delete;
  ActionResult(volatile void *) = delete;

  bool isInvalid() const { return Invalid; }
  bool isUnset() const { return !Invalid && !Val; }
  bool isUsable() const { return !isInvalid() && !isUnset(); }

  PtrTy get() const { return Val; }
  template <typename T> T *getAs() { return static_cast<T *>(get()); }

  ActionResult &operator=(PtrTy RHS) {
    Val = RHS;
    Invalid = false;
    return *this;
  }
};

// If we PtrTy has a free bit, we can represent "invalid" as nullptr|1.
template <typename PtrTy> class ActionResult<PtrTy, true> {
  static constexpr uintptr_t UnsetValue = 0x0;
  static constexpr uintptr_t InvalidValue = 0x1;

  uintptr_t Value = UnsetValue;

  using PtrTraits = llvm::PointerLikeTypeTraits<PtrTy>;

public:
  ActionResult(bool Invalid = false)
      : Value(Invalid ? InvalidValue : UnsetValue) {}
  ActionResult(PtrTy V) { *this = V; }
  ActionResult(const DiagnosticBuilder &) : ActionResult(/*Invalid=*/true) {}

  // These two overloads prevent void* -> bool conversions.
  ActionResult(const void *) = delete;
  ActionResult(volatile void *) = delete;

  bool isInvalid() const { return Value == InvalidValue; }
  bool isUnset() const { return Value == UnsetValue; }
  bool isUsable() const { return !isInvalid() && !isUnset(); }

  PtrTy get() const {
    void *VP = reinterpret_cast<void *>(Value & ~0x01);
    return PtrTraits::getFromVoidPointer(VP);
  }
  template <typename T> T *getAs() { return static_cast<T *>(get()); }

  ActionResult &operator=(PtrTy RHS) {
    void *VP = PtrTraits::getAsVoidPointer(RHS);
    Value = reinterpret_cast<uintptr_t>(VP);
    assert((Value & 0x01) == 0 && "Badly aligned pointer");
    return *this;
  }

  // For types where we can fit a flag in with the pointer, provide
  // conversions to/from pointer type.
  static ActionResult getFromOpaquePointer(void *P) {
    ActionResult Result;
    Result.Value = (uintptr_t)P;
    assert(Result.isInvalid() ||
           PtrTraits::getAsVoidPointer(Result.get()) == P);
    return Result;
  }
  void *getAsOpaquePointer() const { return (void *)Value; }
};

using ParsedType = OpaquePtr<QualType>;
using UnionParsedType = UnionOpaquePtr<QualType>;

// We can re-use the low bit of expression and statement pointers for the
// "invalid" flag of ActionResult.
template <> struct IsResultPtrLowBitFree<Expr *> {
  static const bool value = true;
};
template <> struct IsResultPtrLowBitFree<Stmt *> {
  static const bool value = true;
};
using ExprResult = ActionResult<Expr *>;
using StmtResult = ActionResult<Stmt *>;
using TypeResult = ActionResult<ParsedType>;

using DeclResult = ActionResult<Decl *>;

using MultiExprArg = llvm::MutableArrayRef<Expr *>;
using MultiStmtArg = llvm::MutableArrayRef<Stmt *>;

inline ExprResult ExprError() { return ExprResult(true); }
inline StmtResult StmtError() { return StmtResult(true); }
inline TypeResult TypeError() { return TypeResult(true); }

inline ExprResult ExprError(const StreamingDiagnostic &) { return ExprError(); }
inline StmtResult StmtError(const StreamingDiagnostic &) { return StmtError(); }

inline ExprResult ExprEmpty() { return ExprResult(false); }
inline StmtResult StmtEmpty() { return StmtResult(false); }

inline Expr *AssertSuccess(ExprResult R) {
  assert(!R.isInvalid() && "operation was asserted to never fail!");
  return R.get();
}

inline Stmt *AssertSuccess(StmtResult R) {
  assert(!R.isInvalid() && "operation was asserted to never fail!");
  return R.get();
}

} // namespace neverc

#endif // NEVERC_ANALYZE_OWNERSHIP_H
