#ifndef NEVERC_AST_EXTERNALTREESOURCE_H
#define NEVERC_AST_EXTERNALTREESOURCE_H

#include "llvm/Support/PointerLikeTypeTraits.h"
#include <cstdint>

namespace neverc {

class TreeContext;
class Stmt;

struct LazyDeclStmtPtr {
  mutable Stmt *Ptr = nullptr;

  LazyDeclStmtPtr() = default;
  explicit LazyDeclStmtPtr(Stmt *P) : Ptr(P) {}

  LazyDeclStmtPtr &operator=(Stmt *P) {
    Ptr = P;
    return *this;
  }

  explicit operator bool() const { return Ptr != nullptr; }
  bool isValid() const { return Ptr != nullptr; }

  Stmt *get(void * = nullptr) const { return Ptr; }

  Stmt **getAddressOfPointer(void * = nullptr) const { return &Ptr; }
};

template <typename Owner, typename T> struct LatestDeclPtr {
  T Value = T();

  LatestDeclPtr() = default;
  explicit LatestDeclPtr(const TreeContext &, T V = T()) : Value(V) {}

  enum NotUpdatedTag { NotUpdated };
  LatestDeclPtr(NotUpdatedTag, T V = T()) : Value(V) {}

  static LatestDeclPtr makeValue(const TreeContext &, T V) {
    LatestDeclPtr R;
    R.Value = V;
    return R;
  }

  void markIncomplete() {}
  void set(T V) { Value = V; }
  void setNotUpdated(T V) { Value = V; }
  T get(Owner) { return Value; }
  T getNotUpdated() const { return Value; }

  void *getOpaqueValue() { return Value; }
  static LatestDeclPtr getFromOpaqueValue(void *P) {
    LatestDeclPtr R;
    R.Value = static_cast<T>(P);
    return R;
  }
};

} // namespace neverc

namespace llvm {

template <typename Owner, typename T>
struct PointerLikeTypeTraits<neverc::LatestDeclPtr<Owner, T>> {
  using Ptr = neverc::LatestDeclPtr<Owner, T>;

  static void *getAsVoidPointer(Ptr P) { return P.getOpaqueValue(); }
  static Ptr getFromVoidPointer(void *P) { return Ptr::getFromOpaqueValue(P); }

  static constexpr int NumLowBitsAvailable =
      PointerLikeTypeTraits<T>::NumLowBitsAvailable - 1;
};

} // namespace llvm

#endif // NEVERC_AST_EXTERNALTREESOURCE_H
