#ifndef NEVERC_TREE_DECLARATIONNAME_H
#define NEVERC_TREE_DECLARATIONNAME_H

#include "neverc/Foundation/Core/IdentifierTable.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstdint>
#include <string>

namespace neverc {

class DeclarationName;
struct PrintingPolicy;

class DeclarationName {
  friend class NamedDecl;

private:
  uintptr_t Ptr = 0;

  IdentifierInfo *castAsIdentifierInfo() const {
    return reinterpret_cast<IdentifierInfo *>(Ptr);
  }

public:
  DeclarationName() = default;

  DeclarationName(const IdentifierInfo *II)
      : Ptr(reinterpret_cast<uintptr_t>(II)) {}

  explicit operator bool() const { return Ptr != 0; }

  bool isEmpty() const { return !*this; }

  std::string getAsString() const;

  IdentifierInfo *getAsIdentifierInfo() const { return castAsIdentifierInfo(); }

  uintptr_t getAsOpaqueInteger() const { return Ptr; }

  void *getAsOpaquePtr() const { return reinterpret_cast<void *>(Ptr); }

  static DeclarationName getFromOpaquePtr(void *P) {
    DeclarationName N;
    N.Ptr = reinterpret_cast<uintptr_t>(P);
    return N;
  }

  static DeclarationName getFromOpaqueInteger(uintptr_t P) {
    DeclarationName N;
    N.Ptr = P;
    return N;
  }

  void *getFETokenInfo() const {
    assert(Ptr && "getFETokenInfo on an empty DeclarationName!");
    return castAsIdentifierInfo()->getFETokenInfo();
  }

  void setFETokenInfo(void *T) {
    assert(Ptr && "setFETokenInfo on an empty DeclarationName!");
    castAsIdentifierInfo()->setFETokenInfo(T);
  }

  friend bool operator==(DeclarationName LHS, DeclarationName RHS) {
    return LHS.Ptr == RHS.Ptr;
  }

  friend bool operator!=(DeclarationName LHS, DeclarationName RHS) {
    return LHS.Ptr != RHS.Ptr;
  }

  static DeclarationName getEmptyMarker() {
    DeclarationName Name;
    Name.Ptr = uintptr_t(-1);
    return Name;
  }

  static DeclarationName getTombstoneMarker() {
    DeclarationName Name;
    Name.Ptr = uintptr_t(-2);
    return Name;
  }

  static int compare(DeclarationName LHS, DeclarationName RHS);

  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const;

  void dump() const;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, DeclarationName N);

inline bool operator<(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) < 0;
}

inline bool operator>(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) > 0;
}

inline bool operator<=(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) <= 0;
}

inline bool operator>=(DeclarationName LHS, DeclarationName RHS) {
  return DeclarationName::compare(LHS, RHS) >= 0;
}

struct DeclarationNameInfo {
private:
  DeclarationName Name;
  SourceLocation NameLoc;

public:
  DeclarationNameInfo() = default;

  DeclarationNameInfo(DeclarationName Name, SourceLocation NameLoc)
      : Name(Name), NameLoc(NameLoc) {}

  DeclarationName getName() const { return Name; }
  void setName(DeclarationName N) { Name = N; }

  SourceLocation getLoc() const { return NameLoc; }
  void setLoc(SourceLocation L) { NameLoc = L; }

  std::string getAsString() const;
  void printName(llvm::raw_ostream &OS, PrintingPolicy Policy) const;

  SourceLocation getBeginLoc() const { return NameLoc; }

  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getBeginLoc(), getEndLoc());
  }

  SourceLocation getEndLoc() const LLVM_READONLY { return getBeginLoc(); }
};

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             DeclarationName N) {
  PD.AddTaggedVal(N.getAsOpaqueInteger(),
                  DiagnosticsEngine::ak_declarationname);
  return PD;
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              DeclarationNameInfo DNInfo);

} // namespace neverc

namespace llvm {

template <> struct DenseMapInfo<neverc::DeclarationName> {
  static inline neverc::DeclarationName getEmptyKey() {
    return neverc::DeclarationName::getEmptyMarker();
  }

  static inline neverc::DeclarationName getTombstoneKey() {
    return neverc::DeclarationName::getTombstoneMarker();
  }

  static unsigned getHashValue(neverc::DeclarationName Name) {
    return DenseMapInfo<void *>::getHashValue(Name.getAsOpaquePtr());
  }

  static inline bool isEqual(neverc::DeclarationName LHS,
                             neverc::DeclarationName RHS) {
    return LHS == RHS;
  }
};

template <> struct PointerLikeTypeTraits<neverc::DeclarationName> {
  static inline void *getAsVoidPointer(neverc::DeclarationName P) {
    return P.getAsOpaquePtr();
  }
  static inline neverc::DeclarationName getFromVoidPointer(void *P) {
    return neverc::DeclarationName::getFromOpaquePtr(P);
  }
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

#endif // NEVERC_TREE_DECLARATIONNAME_H
