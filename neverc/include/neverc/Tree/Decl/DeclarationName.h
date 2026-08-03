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

/// C++ overloaded operator kinds (subset used for naming).
enum OverloadedOperatorKind : unsigned {
  OO_None,
  OO_New,
  OO_Delete,
  OO_Array_New,
  OO_Array_Delete,
  OO_Plus,
  OO_Minus,
  OO_Star,
  OO_Slash,
  OO_Percent,
  OO_Caret,
  OO_Amp,
  OO_Pipe,
  OO_Tilde,
  OO_Exclaim,
  OO_Equal,
  OO_Less,
  OO_Greater,
  OO_PlusEqual,
  OO_MinusEqual,
  OO_StarEqual,
  OO_SlashEqual,
  OO_PercentEqual,
  OO_Caretequal,
  OO_Ampequal,
  OO_Pipeequal,
  OO_LessLess,
  OO_GreaterGreater,
  OO_LessLessequal,
  OO_GreaterGreaterequal,
  OO_EqualEqual,
  OO_Exclaimequal,
  OO_Lessequal,
  OO_Greaterequal,
  OO_Spaceship,
  OO_AmpAmp,
  OO_PipePipe,
  OO_PlusPlus,
  OO_MinusMinus,
  OO_Comma,
  OO_ArrowStar,
  OO_Arrow,
  OO_Call,
  OO_Subscript,
  OO_Conditional,
  OO_Coawait,
  NUM_OVERLOADED_OPERATORS
};

class DeclarationName;
struct PrintingPolicy;

/// C++ special name storage (constructor/destructor/conversion/operator).
/// Owned by TreeContext / IdentifierTable lifetime is not required; these are
/// allocated via TreeContext bump when created through Sema helpers, or as
/// static-duration for operator spellings map. For Phase-2 we allocate with
/// `new` and intentionally leak into the process (TreeContext-owned pool TBD).
struct CXXSpecialName {
  enum class StoredKind : unsigned char {
    Constructor = 1,
    Destructor = 2,
    Conversion = 3,
    Operator = 4,
    LiteralOperator = 5,
  };
  StoredKind Kind;
  union {
    void *TypeOrII; // QualType opaque or IdentifierInfo* for literal
    uintptr_t Opaque;
  };
  OverloadedOperatorKind Operator = OO_None;

  static CXXSpecialName *CreateConstructor(void *TypeOpaque) {
    auto *S = new CXXSpecialName();
    S->Kind = StoredKind::Constructor;
    S->TypeOrII = TypeOpaque;
    return S;
  }
  static CXXSpecialName *CreateDestructor(void *TypeOpaque) {
    auto *S = new CXXSpecialName();
    S->Kind = StoredKind::Destructor;
    S->TypeOrII = TypeOpaque;
    return S;
  }
  static CXXSpecialName *CreateConversion(void *TypeOpaque) {
    auto *S = new CXXSpecialName();
    S->Kind = StoredKind::Conversion;
    S->TypeOrII = TypeOpaque;
    return S;
  }
  static CXXSpecialName *CreateOperator(OverloadedOperatorKind Op) {
    auto *S = new CXXSpecialName();
    S->Kind = StoredKind::Operator;
    S->TypeOrII = nullptr;
    S->Operator = Op;
    return S;
  }
  static CXXSpecialName *CreateLiteralOperator(IdentifierInfo *II) {
    auto *S = new CXXSpecialName();
    S->Kind = StoredKind::LiteralOperator;
    S->TypeOrII = II;
    return S;
  }
};

class DeclarationName {
  friend class NamedDecl;

public:
  /// Declaration name kinds. Identifier uses Ptr with low bits 0.
  enum NameKind : unsigned {
    Identifier = 0,
    CXXConstructorName = 1,
    CXXDestructorName = 2,
    CXXConversionFunctionName = 3,
    CXXOperatorName = 4,
    CXXLiteralOperatorName = 5,
    CXXUsingDirective = 6,
  };

private:
  static constexpr uintptr_t PtrMask = 0x7;

  uintptr_t Ptr = 0;

  DeclarationName(uintptr_t Raw) : Ptr(Raw) {}

  static DeclarationName makeSpecial(NameKind K, CXXSpecialName *S) {
    assert(K != Identifier && "use IdentifierInfo ctor");
    assert(((uintptr_t)S & PtrMask) == 0 && "CXXSpecialName underaligned");
    return DeclarationName(reinterpret_cast<uintptr_t>(S) | (uintptr_t)K);
  }

  IdentifierInfo *castAsIdentifierInfo() const {
    assert(getNameKind() == Identifier);
    return reinterpret_cast<IdentifierInfo *>(Ptr);
  }

  CXXSpecialName *castAsSpecial() const {
    assert(getNameKind() != Identifier);
    return reinterpret_cast<CXXSpecialName *>(Ptr & ~PtrMask);
  }

public:
  DeclarationName() = default;

  DeclarationName(const IdentifierInfo *II)
      : Ptr(reinterpret_cast<uintptr_t>(II)) {
    assert(((uintptr_t)II & PtrMask) == 0 && "IdentifierInfo underaligned");
  }

  explicit operator bool() const { return Ptr != 0; }

  bool isEmpty() const { return !*this; }

  NameKind getNameKind() const {
    if (!Ptr)
      return Identifier;
    unsigned Tag = static_cast<unsigned>(Ptr & PtrMask);
    if (Tag == 0)
      return Identifier;
    return static_cast<NameKind>(Tag);
  }

  bool isIdentifier() const { return getNameKind() == Identifier; }
  bool isCXXSpecialName() const {
    NameKind K = getNameKind();
    return K == CXXConstructorName || K == CXXDestructorName ||
           K == CXXConversionFunctionName || K == CXXOperatorName ||
           K == CXXLiteralOperatorName;
  }

  std::string getAsString() const;

  IdentifierInfo *getAsIdentifierInfo() const {
    if (getNameKind() != Identifier)
      return nullptr;
    return castAsIdentifierInfo();
  }

  OverloadedOperatorKind getCXXOverloadedOperator() const {
    if (getNameKind() != CXXOperatorName)
      return OO_None;
    return castAsSpecial()->Operator;
  }

  IdentifierInfo *getCXXLiteralIdentifier() const {
    if (getNameKind() != CXXLiteralOperatorName)
      return nullptr;
    return static_cast<IdentifierInfo *>(castAsSpecial()->TypeOrII);
  }

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
    if (IdentifierInfo *II = getAsIdentifierInfo())
      return II->getFETokenInfo();
    return nullptr;
  }

  void setFETokenInfo(void *T) {
    if (IdentifierInfo *II = getAsIdentifierInfo())
      II->setFETokenInfo(T);
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

  /// Construct special C++ names (multi-kind tags).
  static DeclarationName getCXXConstructorName(void *TypeOpaque) {
    return makeSpecial(CXXConstructorName,
                       CXXSpecialName::CreateConstructor(TypeOpaque));
  }
  static DeclarationName getCXXDestructorName(void *TypeOpaque) {
    return makeSpecial(CXXDestructorName,
                       CXXSpecialName::CreateDestructor(TypeOpaque));
  }
  static DeclarationName getCXXConversionFunctionName(void *TypeOpaque) {
    return makeSpecial(CXXConversionFunctionName,
                       CXXSpecialName::CreateConversion(TypeOpaque));
  }
  /// Backward-compatible helpers that still accept IdentifierInfo spellings.
  static DeclarationName getCXXConstructorName(IdentifierInfo *ClassName) {
    return DeclarationName(ClassName);
  }
  static DeclarationName getCXXDestructorName(IdentifierInfo *ClassName) {
    return DeclarationName(ClassName);
  }
  static DeclarationName getCXXOperatorName(IdentifierInfo *OpSpelling) {
    return DeclarationName(OpSpelling);

  static DeclarationName getCXXLiteralOperatorName(IdentifierInfo *II) {
    return makeSpecial(CXXLiteralOperatorName,
                       CXXSpecialName::CreateLiteralOperator(II));
  }
  }
  static DeclarationName getCXXOperatorName(OverloadedOperatorKind Op) {
    return makeSpecial(CXXOperatorName, CXXSpecialName::CreateOperator(Op));
  }

  static const char *getOperatorSpelling(OverloadedOperatorKind Op);
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
