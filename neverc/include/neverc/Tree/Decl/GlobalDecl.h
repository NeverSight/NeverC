#ifndef NEVERC_TREE_GLOBALDECL_H
#define NEVERC_TREE_GLOBALDECL_H

#include "neverc/Tree/Core/Attr.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/Casting.h"
#include <cassert>

namespace neverc {

using llvm::cast;
using llvm::isa;

class GlobalDecl {
  const Decl *TheDecl = nullptr;
  unsigned MultiVersionIndex = 0;

  void Init(const Decl *D) { TheDecl = D; }

public:
  GlobalDecl() = default;
  GlobalDecl(const VarDecl *D) { Init(D); }
  GlobalDecl(const FunctionDecl *D, unsigned MVIndex = 0)
      : MultiVersionIndex(MVIndex) {
    Init(D);
  }
  GlobalDecl(const NamedDecl *D) { Init(D); }

  GlobalDecl getCanonicalDecl() const {
    GlobalDecl CanonGD;
    if (TheDecl)
      CanonGD.TheDecl = TheDecl->getCanonicalDecl();
    CanonGD.MultiVersionIndex = MultiVersionIndex;

    return CanonGD;
  }

  const Decl *getDecl() const { return TheDecl; }

  unsigned getMultiVersionIndex() const {
    assert(isa<FunctionDecl>(getDecl()) && "Decl is not a plain FunctionDecl!");
    return MultiVersionIndex;
  }

  friend bool operator==(const GlobalDecl &LHS, const GlobalDecl &RHS) {
    return LHS.TheDecl == RHS.TheDecl &&
           LHS.MultiVersionIndex == RHS.MultiVersionIndex;
  }

  void *getAsOpaquePtr() const { return const_cast<Decl *>(TheDecl); }

  explicit operator bool() const { return TheDecl != nullptr; }

  static GlobalDecl getFromOpaquePtr(void *P) {
    GlobalDecl GD;
    GD.TheDecl = static_cast<const Decl *>(P);
    return GD;
  }

  GlobalDecl getWithDecl(const Decl *D) {
    GlobalDecl Result(*this);
    Result.TheDecl = D;
    return Result;
  }

  GlobalDecl getWithMultiVersionIndex(unsigned Index) {
    assert(isa<FunctionDecl>(getDecl()) && "Decl is not a plain FunctionDecl!");
    GlobalDecl Result(*this);
    Result.MultiVersionIndex = Index;
    return Result;
  }
};

} // namespace neverc

namespace llvm {

template <> struct DenseMapInfo<neverc::GlobalDecl> {
  static inline neverc::GlobalDecl getEmptyKey() {
    return neverc::GlobalDecl();
  }

  static inline neverc::GlobalDecl getTombstoneKey() {
    return neverc::GlobalDecl::getFromOpaquePtr(reinterpret_cast<void *>(-1));
  }

  static unsigned getHashValue(neverc::GlobalDecl GD) {
    return DenseMapInfo<void *>::getHashValue(GD.getAsOpaquePtr());
  }

  static bool isEqual(neverc::GlobalDecl LHS, neverc::GlobalDecl RHS) {
    return LHS == RHS;
  }
};

} // namespace llvm

#endif // NEVERC_TREE_GLOBALDECL_H
