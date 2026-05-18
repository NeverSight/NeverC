#ifndef NEVERC_AST_TYPE_H
#define NEVERC_AST_TYPE_H

#include "neverc/Foundation/Attr/AttrKinds.h"
#include "neverc/Foundation/Core/AddressSpaces.h"
#include "neverc/Foundation/Core/ExceptionSpecificationType.h"
#include "neverc/Foundation/Core/Linkage.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Foundation/Core/Visibility.h"
#include "neverc/Foundation/Diagnostic/Diagnostic.h"
#include "neverc/Foundation/Diagnostic/PartialDiagnostic.h"
#include "neverc/Tree/Type/DependenceFlags.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace neverc {

using llvm::cast;
using llvm::cast_or_null;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;
using llvm::isa;
using llvm::SaveAndRestore;

class BTFTypeTagAttr;
class ExtQuals;
class QualType;
class TagDecl;
class Type;

enum { TypeAlignmentInBits = 4, TypeAlignment = 1 << TypeAlignmentInBits };

} // namespace neverc

namespace llvm {

template <typename T> struct PointerLikeTypeTraits;
template <> struct PointerLikeTypeTraits<::neverc::Type *> {
  static inline void *getAsVoidPointer(::neverc::Type *P) { return P; }

  static inline ::neverc::Type *getFromVoidPointer(void *P) {
    return static_cast<::neverc::Type *>(P);
  }

  static constexpr int NumLowBitsAvailable = neverc::TypeAlignmentInBits;
};

template <> struct PointerLikeTypeTraits<::neverc::ExtQuals *> {
  static inline void *getAsVoidPointer(::neverc::ExtQuals *P) { return P; }

  static inline ::neverc::ExtQuals *getFromVoidPointer(void *P) {
    return static_cast<::neverc::ExtQuals *>(P);
  }

  static constexpr int NumLowBitsAvailable = neverc::TypeAlignmentInBits;
};

} // namespace llvm

namespace neverc {

class TreeContext;
template <typename> class CanQual;
class EnumDecl;
class Expr;
class ExtQualsTypeCommonBase;
class IdentifierInfo;
class NamedDecl;
struct PrintingPolicy;
class RecordDecl;
class Stmt;
class TagDecl;
class TypeLoc;
class TypedefNameDecl;

using CanQualType = CanQual<Type>;

// Provide forward declarations for all of the *Type classes.
#define TYPE(Class, Base) class Class##Type;
#include "neverc/Tree/TypeNodes.td.h"

class Qualifiers {
public:
  enum TQ { // NOTE: These flags must be kept in sync with DeclSpec::TQ.
    Const = 0x1,
    Restrict = 0x2,
    Volatile = 0x4,
    CVRMask = Const | Volatile | Restrict
  };

  enum {
    /// The maximum supported address space number.
    /// 23 bits should be enough for anyone.
    MaxAddressSpace = 0x7fffffu,

    /// The width of the "fast" qualifier mask.
    FastWidth = 3,

    /// The fast qualifier mask.
    FastMask = (1 << FastWidth) - 1
  };

  static Qualifiers removeCommonQualifiers(Qualifiers &L, Qualifiers &R) {
    // If both are only CVR-qualified, bit operations are sufficient.
    if (!(L.Mask & ~CVRMask) && !(R.Mask & ~CVRMask)) {
      Qualifiers Q;
      Q.Mask = L.Mask & R.Mask;
      L.Mask &= ~Q.Mask;
      R.Mask &= ~Q.Mask;
      return Q;
    }

    Qualifiers Q;
    unsigned CommonCRV = L.getCVRQualifiers() & R.getCVRQualifiers();
    Q.addCVRQualifiers(CommonCRV);
    L.removeCVRQualifiers(CommonCRV);
    R.removeCVRQualifiers(CommonCRV);

    if (L.getAddressSpace() == R.getAddressSpace()) {
      Q.setAddressSpace(L.getAddressSpace());
      L.removeAddressSpace();
      R.removeAddressSpace();
    }
    return Q;
  }

  static Qualifiers fromFastMask(unsigned Mask) {
    Qualifiers Qs;
    Qs.addFastQualifiers(Mask);
    return Qs;
  }

  static Qualifiers fromCVRMask(unsigned CVR) {
    Qualifiers Qs;
    Qs.addCVRQualifiers(CVR);
    return Qs;
  }

  static Qualifiers fromCVRUMask(unsigned CVRU) {
    Qualifiers Qs;
    Qs.addCVRUQualifiers(CVRU);
    return Qs;
  }

  // Deserialize qualifiers from an opaque representation.
  static Qualifiers fromOpaqueValue(unsigned opaque) {
    Qualifiers Qs;
    Qs.Mask = opaque;
    return Qs;
  }

  // Serialize these qualifiers into an opaque representation.
  unsigned getAsOpaqueValue() const { return Mask; }

  bool hasConst() const { return Mask & Const; }
  bool hasOnlyConst() const { return Mask == Const; }
  void removeConst() { Mask &= ~Const; }
  void addConst() { Mask |= Const; }
  Qualifiers withConst() const {
    Qualifiers Qs = *this;
    Qs.addConst();
    return Qs;
  }

  bool hasVolatile() const { return Mask & Volatile; }
  bool hasOnlyVolatile() const { return Mask == Volatile; }
  void removeVolatile() { Mask &= ~Volatile; }
  void addVolatile() { Mask |= Volatile; }
  Qualifiers withVolatile() const {
    Qualifiers Qs = *this;
    Qs.addVolatile();
    return Qs;
  }

  bool hasRestrict() const { return Mask & Restrict; }
  bool hasOnlyRestrict() const { return Mask == Restrict; }
  void removeRestrict() { Mask &= ~Restrict; }
  void addRestrict() { Mask |= Restrict; }
  Qualifiers withRestrict() const {
    Qualifiers Qs = *this;
    Qs.addRestrict();
    return Qs;
  }

  bool hasCVRQualifiers() const { return getCVRQualifiers(); }
  unsigned getCVRQualifiers() const { return Mask & CVRMask; }
  unsigned getCVRUQualifiers() const { return Mask & (CVRMask | UMask); }

  void setCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask = (Mask & ~CVRMask) | mask;
  }
  void removeCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask &= ~mask;
  }
  void removeCVRQualifiers() { removeCVRQualifiers(CVRMask); }
  void addCVRQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask) && "bitmask contains non-CVR bits");
    Mask |= mask;
  }
  void addCVRUQualifiers(unsigned mask) {
    assert(!(mask & ~CVRMask & ~UMask) && "bitmask contains non-CVRU bits");
    Mask |= mask;
  }

  bool hasUnaligned() const { return Mask & UMask; }
  void setUnaligned(bool flag) { Mask = (Mask & ~UMask) | (flag ? UMask : 0); }
  void removeUnaligned() { Mask &= ~UMask; }
  void addUnaligned() { Mask |= UMask; }

  Qualifiers withoutAddressSpace() const {
    Qualifiers qs = *this;
    qs.removeAddressSpace();
    return qs;
  }

  bool hasAddressSpace() const { return Mask & AddressSpaceMask; }
  LangAS getAddressSpace() const {
    return static_cast<LangAS>(Mask >> AddressSpaceShift);
  }
  bool hasTargetSpecificAddressSpace() const {
    return isTargetAddressSpace(getAddressSpace());
  }
  unsigned getAddressSpaceAttributePrintValue() const {
    auto Addr = getAddressSpace();
    // This function is not supposed to be used with language specific
    // address spaces. If that happens, the diagnostic message should consider
    // printing the QualType instead of the address space value.
    assert(Addr == LangAS::Default || hasTargetSpecificAddressSpace());
    if (Addr != LangAS::Default)
      return toTargetAddressSpace(Addr);
    return 0;
  }
  void setAddressSpace(LangAS space) {
    assert((unsigned)space <= MaxAddressSpace);
    Mask =
        (Mask & ~AddressSpaceMask) | (((uint32_t)space) << AddressSpaceShift);
  }
  void removeAddressSpace() { setAddressSpace(LangAS::Default); }
  void addAddressSpace(LangAS space) {
    assert(space != LangAS::Default);
    setAddressSpace(space);
  }

  // Fast qualifiers are those that can be allocated directly
  // on a QualType object.
  bool hasFastQualifiers() const { return getFastQualifiers(); }
  unsigned getFastQualifiers() const { return Mask & FastMask; }
  void setFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask = (Mask & ~FastMask) | mask;
  }
  void removeFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask &= ~mask;
  }
  void removeFastQualifiers() { removeFastQualifiers(FastMask); }
  void addFastQualifiers(unsigned mask) {
    assert(!(mask & ~FastMask) && "bitmask contains non-fast qualifier bits");
    Mask |= mask;
  }

  bool hasNonFastQualifiers() const { return Mask & ~FastMask; }
  Qualifiers getNonFastQualifiers() const {
    Qualifiers Quals = *this;
    Quals.setFastQualifiers(0);
    return Quals;
  }

  bool hasQualifiers() const { return Mask; }
  bool empty() const { return !Mask; }

  void addQualifiers(Qualifiers Q) {
    // If the other set doesn't have any non-boolean qualifiers, just
    // bit-or it in.
    if (!(Q.Mask & ~CVRMask))
      Mask |= Q.Mask;
    else {
      Mask |= (Q.Mask & CVRMask);
      if (Q.hasAddressSpace())
        addAddressSpace(Q.getAddressSpace());
    }
  }

  void removeQualifiers(Qualifiers Q) {
    // If the other set doesn't have any non-boolean qualifiers, just
    // bit-and the inverse in.
    if (!(Q.Mask & ~CVRMask))
      Mask &= ~Q.Mask;
    else {
      Mask &= ~(Q.Mask & CVRMask);
      if (getAddressSpace() == Q.getAddressSpace())
        removeAddressSpace();
    }
  }

  void addConsistentQualifiers(Qualifiers qs) {
    assert(getAddressSpace() == qs.getAddressSpace() || !hasAddressSpace() ||
           !qs.hasAddressSpace());
    Mask |= qs.Mask;
  }

  static bool isAddressSpaceSupersetOf(LangAS A, LangAS B) {
    return A == B || ((isPtrSizeAddressSpace(A) || A == LangAS::Default) &&
                      (isPtrSizeAddressSpace(B) || B == LangAS::Default));
  }

  bool isAddressSpaceSupersetOf(Qualifiers other) const {
    return isAddressSpaceSupersetOf(getAddressSpace(), other.getAddressSpace());
  }

  bool compatiblyIncludes(Qualifiers other) const {
    return isAddressSpaceSupersetOf(other) &&
           // CVR qualifiers may subset.
           (((Mask & CVRMask) | (other.Mask & CVRMask)) == (Mask & CVRMask)) &&
           // U qualifier may superset.
           (!other.hasUnaligned() || hasUnaligned());
  }

  bool isStrictSupersetOf(Qualifiers Other) const;

  bool operator==(Qualifiers Other) const { return Mask == Other.Mask; }
  bool operator!=(Qualifiers Other) const { return Mask != Other.Mask; }

  explicit operator bool() const { return hasQualifiers(); }

  Qualifiers &operator+=(Qualifiers R) {
    addQualifiers(R);
    return *this;
  }

  // Union two qualifier sets.  If an enumerated qualifier appears
  // in both sets, use the one from the right.
  friend Qualifiers operator+(Qualifiers L, Qualifiers R) {
    L += R;
    return L;
  }

  Qualifiers &operator-=(Qualifiers R) {
    removeQualifiers(R);
    return *this;
  }

  friend Qualifiers operator-(Qualifiers L, Qualifiers R) {
    L -= R;
    return L;
  }

  std::string getAsString() const;
  std::string getAsString(const PrintingPolicy &Policy) const;

  static std::string getAddrSpaceAsString(LangAS AS);

  bool isEmptyWhenPrinted(const PrintingPolicy &Policy) const;
  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy,
             bool appendSpaceIfNonEmpty = false) const;

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(Mask); }

private:
  // bits:     |0 1 2|3|4   ...   31|
  //           |C R V|U|AddressSpace|
  uint32_t Mask = 0;

  static const uint32_t UMask = 0x8;
  static const uint32_t UShift = 3;
  static const uint32_t AddressSpaceMask = ~(CVRMask | UMask);
  static const uint32_t AddressSpaceShift = 4;
};

class QualifiersAndAtomic {
  Qualifiers Quals;
  bool HasAtomic;

public:
  QualifiersAndAtomic() : HasAtomic(false) {}
  QualifiersAndAtomic(Qualifiers Quals, bool HasAtomic)
      : Quals(Quals), HasAtomic(HasAtomic) {}

  operator Qualifiers() const { return Quals; }

  bool hasVolatile() const { return Quals.hasVolatile(); }
  bool hasConst() const { return Quals.hasConst(); }
  bool hasRestrict() const { return Quals.hasRestrict(); }
  bool hasAtomic() const { return HasAtomic; }

  void addVolatile() { Quals.addVolatile(); }
  void addConst() { Quals.addConst(); }
  void addRestrict() { Quals.addRestrict(); }
  void addAtomic() { HasAtomic = true; }

  void removeVolatile() { Quals.removeVolatile(); }
  void removeConst() { Quals.removeConst(); }
  void removeRestrict() { Quals.removeRestrict(); }
  void removeAtomic() { HasAtomic = false; }

  QualifiersAndAtomic withVolatile() {
    return {Quals.withVolatile(), HasAtomic};
  }
  QualifiersAndAtomic withConst() { return {Quals.withConst(), HasAtomic}; }
  QualifiersAndAtomic withRestrict() {
    return {Quals.withRestrict(), HasAtomic};
  }
  QualifiersAndAtomic withAtomic() { return {Quals, true}; }

  QualifiersAndAtomic &operator+=(Qualifiers RHS) {
    Quals += RHS;
    return *this;
  }
};

struct SplitQualType {
  const Type *Ty = nullptr;

  Qualifiers Quals;

  SplitQualType() = default;
  SplitQualType(const Type *ty, Qualifiers qs) : Ty(ty), Quals(qs) {}

  SplitQualType getSingleStepDesugaredType() const; // end of this file

  // Make std::tie work.
  std::pair<const Type *, Qualifiers> asPair() const {
    return std::pair<const Type *, Qualifiers>(Ty, Quals);
  }

  friend bool operator==(SplitQualType a, SplitQualType b) {
    return a.Ty == b.Ty && a.Quals == b.Quals;
  }
  friend bool operator!=(SplitQualType a, SplitQualType b) {
    return a.Ty != b.Ty || a.Quals != b.Quals;
  }
};

enum class TypeOfKind : uint8_t {
  Qualified,
  Unqualified,
};

class QualType {
  friend class QualifierCollector;

  // Thankfully, these are efficiently composable.
  llvm::PointerIntPair<llvm::PointerUnion<const Type *, const ExtQuals *>,
                       Qualifiers::FastWidth>
      Value;

  const ExtQuals *getExtQualsUnsafe() const {
    return Value.getPointer().get<const ExtQuals *>();
  }

  const Type *getTypePtrUnsafe() const {
    return Value.getPointer().get<const Type *>();
  }

  const ExtQualsTypeCommonBase *getCommonPtr() const {
    assert(!isNull() && "Cannot retrieve a NULL type pointer");
    auto CommonPtrVal = reinterpret_cast<uintptr_t>(Value.getOpaqueValue());
    CommonPtrVal &= ~(uintptr_t)((1 << TypeAlignmentInBits) - 1);
    return reinterpret_cast<ExtQualsTypeCommonBase *>(CommonPtrVal);
  }

public:
  QualType() = default;
  QualType(const Type *Ptr, unsigned Quals) : Value(Ptr, Quals) {}
  QualType(const ExtQuals *Ptr, unsigned Quals) : Value(Ptr, Quals) {}

  unsigned getLocalFastQualifiers() const { return Value.getInt(); }
  void setLocalFastQualifiers(unsigned Quals) { Value.setInt(Quals); }

  bool UseExcessPrecision(const TreeContext &Ctx);

  const Type *getTypePtr() const;

  const Type *getTypePtrOrNull() const;

  const IdentifierInfo *getBaseTypeIdentifier() const;

  SplitQualType split() const;

  void *getAsOpaquePtr() const { return Value.getOpaqueValue(); }

  static QualType getFromOpaquePtr(const void *Ptr) {
    QualType T;
    T.Value.setFromOpaqueValue(const_cast<void *>(Ptr));
    return T;
  }

  const Type &operator*() const { return *getTypePtr(); }

  const Type *operator->() const { return getTypePtr(); }

  bool isCanonical() const;
  bool isCanonicalAsParam() const;

  bool isNull() const { return Value.getPointer().isNull(); }

  // Determines if a type can form `T&`.
  bool isReferenceable() const;

  bool isLocalConstQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Const);
  }

  bool isConstQualified() const;

  enum class NonConstantStorageReason {
    NonConstType,
  };
  std::optional<NonConstantStorageReason>
  isNonConstantStorage(const TreeContext &Ctx, bool ExcludeCtor,
                       bool ExcludeDtor);

  bool isConstantStorage(const TreeContext &Ctx, bool ExcludeCtor,
                         bool ExcludeDtor) {
    return !isNonConstantStorage(Ctx, ExcludeCtor, ExcludeDtor);
  }

  bool isLocalRestrictQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Restrict);
  }

  bool isRestrictQualified() const;

  bool isLocalVolatileQualified() const {
    return (getLocalFastQualifiers() & Qualifiers::Volatile);
  }

  bool isVolatileQualified() const;

  bool hasLocalQualifiers() const {
    return getLocalFastQualifiers() || hasLocalNonFastQualifiers();
  }

  bool hasQualifiers() const;

  bool hasLocalNonFastQualifiers() const {
    return Value.getPointer().is<const ExtQuals *>();
  }

  Qualifiers getLocalQualifiers() const;

  Qualifiers getQualifiers() const;

  unsigned getLocalCVRQualifiers() const { return getLocalFastQualifiers(); }

  unsigned getCVRQualifiers() const;

  bool isConstant(const TreeContext &Ctx) const {
    return QualType::isConstant(*this, Ctx);
  }

  bool isPODType(const TreeContext &Context) const;

  bool isTriviallyCopyableType(const TreeContext &Context) const;

  // Don't promise in the API that anything besides 'const' can be
  // easily added.

  void addConst() { addFastQualifiers(Qualifiers::Const); }
  QualType withConst() const { return withFastQualifiers(Qualifiers::Const); }

  void addVolatile() { addFastQualifiers(Qualifiers::Volatile); }
  QualType withVolatile() const {
    return withFastQualifiers(Qualifiers::Volatile);
  }

  void addRestrict() { addFastQualifiers(Qualifiers::Restrict); }
  QualType withRestrict() const {
    return withFastQualifiers(Qualifiers::Restrict);
  }

  QualType withCVRQualifiers(unsigned CVR) const {
    return withFastQualifiers(CVR);
  }

  void addFastQualifiers(unsigned TQs) {
    assert(!(TQs & ~Qualifiers::FastMask) &&
           "non-fast qualifier bits set in mask!");
    Value.setInt(Value.getInt() | TQs);
  }

  void removeLocalConst();
  void removeLocalVolatile();
  void removeLocalRestrict();

  void removeLocalFastQualifiers() { Value.setInt(0); }
  void removeLocalFastQualifiers(unsigned Mask) {
    assert(!(Mask & ~Qualifiers::FastMask) && "mask has non-fast qualifiers");
    Value.setInt(Value.getInt() & ~Mask);
  }

  // Creates a type with the given qualifiers in addition to any
  // qualifiers already on this type.
  QualType withFastQualifiers(unsigned TQs) const {
    QualType T = *this;
    T.addFastQualifiers(TQs);
    return T;
  }

  // Creates a type with exactly the given fast qualifiers, removing
  // any existing fast qualifiers.
  QualType withExactLocalFastQualifiers(unsigned TQs) const {
    return withoutLocalFastQualifiers().withFastQualifiers(TQs);
  }

  // Removes fast qualifiers, but leaves any extended qualifiers in place.
  QualType withoutLocalFastQualifiers() const {
    QualType T = *this;
    T.removeLocalFastQualifiers();
    return T;
  }

  QualType getCanonicalType() const;

  QualType getLocalUnqualifiedType() const { return QualType(getTypePtr(), 0); }

  inline QualType getUnqualifiedType() const;

  inline SplitQualType getSplitUnqualifiedType() const;

  bool isMoreQualifiedThan(QualType Other) const;

  bool isAtLeastAsQualifiedAs(QualType Other) const;

  QualType getNonLValueExprType(const TreeContext &Context) const;

  QualType getDesugaredType(const TreeContext &Context) const {
    return getDesugaredType(*this, Context);
  }

  SplitQualType getSplitDesugaredType() const {
    return getSplitDesugaredType(*this);
  }

  QualType getSingleStepDesugaredType(const TreeContext &Context) const {
    return getSingleStepDesugaredTypeImpl(*this, Context);
  }

  QualType IgnoreParens() const {
    if (isa<ParenType>(*this))
      return QualType::IgnoreParens(*this);
    return *this;
  }

  friend bool operator==(const QualType &LHS, const QualType &RHS) {
    return LHS.Value == RHS.Value;
  }
  friend bool operator!=(const QualType &LHS, const QualType &RHS) {
    return LHS.Value != RHS.Value;
  }
  friend bool operator<(const QualType &LHS, const QualType &RHS) {
    return LHS.Value < RHS.Value;
  }

  static std::string getAsString(SplitQualType split,
                                 const PrintingPolicy &Policy) {
    return getAsString(split.Ty, split.Quals, Policy);
  }
  static std::string getAsString(const Type *ty, Qualifiers qs,
                                 const PrintingPolicy &Policy);

  std::string getAsString() const;
  std::string getAsString(const PrintingPolicy &Policy) const;

  void print(llvm::raw_ostream &OS, const PrintingPolicy &Policy,
             const llvm::Twine &PlaceHolder = llvm::Twine(),
             unsigned Indentation = 0) const;

  static void print(SplitQualType split, llvm::raw_ostream &OS,
                    const PrintingPolicy &policy,
                    const llvm::Twine &PlaceHolder, unsigned Indentation = 0) {
    return print(split.Ty, split.Quals, OS, policy, PlaceHolder, Indentation);
  }

  static void print(const Type *ty, Qualifiers qs, llvm::raw_ostream &OS,
                    const PrintingPolicy &policy,
                    const llvm::Twine &PlaceHolder, unsigned Indentation = 0);

  void getAsStringInternal(std::string &Str,
                           const PrintingPolicy &Policy) const;

  static void getAsStringInternal(SplitQualType split, std::string &out,
                                  const PrintingPolicy &policy) {
    return getAsStringInternal(split.Ty, split.Quals, out, policy);
  }

  static void getAsStringInternal(const Type *ty, Qualifiers qs,
                                  std::string &out,
                                  const PrintingPolicy &policy);

  class StreamedQualTypeHelper {
    const QualType &T;
    const PrintingPolicy &Policy;
    const llvm::Twine &PlaceHolder;
    unsigned Indentation;

  public:
    StreamedQualTypeHelper(const QualType &T, const PrintingPolicy &Policy,
                           const llvm::Twine &PlaceHolder, unsigned Indentation)
        : T(T), Policy(Policy), PlaceHolder(PlaceHolder),
          Indentation(Indentation) {}

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                         const StreamedQualTypeHelper &SQT) {
      SQT.T.print(OS, SQT.Policy, SQT.PlaceHolder, SQT.Indentation);
      return OS;
    }
  };

  StreamedQualTypeHelper stream(const PrintingPolicy &Policy,
                                const llvm::Twine &PlaceHolder = llvm::Twine(),
                                unsigned Indentation = 0) const {
    return StreamedQualTypeHelper(*this, Policy, PlaceHolder, Indentation);
  }

  void dump(const char *s) const;
  void dump() const;
  void dump(llvm::raw_ostream &OS, const TreeContext &Context) const;

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(getAsOpaquePtr());
  }

  inline bool hasAddressSpace() const;

  inline LangAS getAddressSpace() const;

  bool isAddressSpaceOverlapping(QualType T) const {
    Qualifiers Q = getQualifiers();
    Qualifiers TQ = T.getQualifiers();
    // Address spaces overlap if at least one of them is a superset of another
    return Q.isAddressSpaceSupersetOf(TQ) || TQ.isAddressSpaceSupersetOf(Q);
  }

  enum PrimitiveDefaultInitializeKind {
    /// The type does not fall into any of the following categories. Note that
    /// this case is zero-valued so that values of this enum can be used as a
    /// boolean condition for non-triviality.
    PDIK_Trivial,

    /// The type is a struct containing a field whose type is not PCK_Trivial.
    PDIK_Struct
  };

  PrimitiveDefaultInitializeKind
  isNonTrivialToPrimitiveDefaultInitialize() const;

  enum PrimitiveCopyKind {
    /// The type does not fall into any of the following categories. Note that
    /// this case is zero-valued so that values of this enum can be used as a
    /// boolean condition for non-triviality.
    PCK_Trivial,

    /// The type would be trivial except that it is volatile-qualified. Types
    /// that fall into one of the other non-trivial cases may additionally be
    /// volatile-qualified.
    PCK_VolatileTrivial,

    /// The type is a struct containing a field whose type is neither
    /// PCK_Trivial nor PCK_VolatileTrivial.
    /// Record types with non-trivial copy semantics fall into this category.
    PCK_Struct
  };

  PrimitiveCopyKind isNonTrivialToPrimitiveCopy() const;

  PrimitiveCopyKind isNonTrivialToPrimitiveDestructiveMove() const;

  enum DestructionKind { DK_none, DK_nontrivial_c_struct };

  DestructionKind isDestructedType() const {
    return isDestructedTypeImpl(*this);
  }

  bool hasNonTrivialToPrimitiveDefaultInitializeCUnion() const;

  bool hasNonTrivialToPrimitiveDestructCUnion() const;

  bool hasNonTrivialToPrimitiveCopyCUnion() const;

  bool isCForbiddenLValueType() const;

  QualType getAtomicUnqualifiedType() const;

private:
  // These methods are implemented in a separate translation unit;
  // "static"-ize them to avoid creating temporary QualTypes in the
  // caller.
  static bool isConstant(QualType T, const TreeContext &Ctx);
  static QualType getDesugaredType(QualType T, const TreeContext &Context);
  static SplitQualType getSplitDesugaredType(QualType T);
  static SplitQualType getSplitUnqualifiedTypeImpl(QualType type);
  static QualType getSingleStepDesugaredTypeImpl(QualType type,
                                                 const TreeContext &C);
  static QualType IgnoreParens(QualType T);
  static DestructionKind isDestructedTypeImpl(QualType type);

  static bool
  hasNonTrivialToPrimitiveDefaultInitializeCUnion(const RecordDecl *RD);
  static bool hasNonTrivialToPrimitiveDestructCUnion(const RecordDecl *RD);
  static bool hasNonTrivialToPrimitiveCopyCUnion(const RecordDecl *RD);
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS, QualType QT);

} // namespace neverc

namespace llvm {

template <> struct simplify_type<::neverc::QualType> {
  using SimpleType = const ::neverc::Type *;

  static SimpleType getSimplifiedValue(::neverc::QualType Val) {
    return Val.getTypePtr();
  }
};

// Teach SmallPtrSet that QualType is "basically a pointer".
template <> struct PointerLikeTypeTraits<neverc::QualType> {
  static inline void *getAsVoidPointer(neverc::QualType P) {
    return P.getAsOpaquePtr();
  }

  static inline neverc::QualType getFromVoidPointer(void *P) {
    return neverc::QualType::getFromOpaquePtr(P);
  }

  // Various qualifiers go in low bits.
  static constexpr int NumLowBitsAvailable = 0;
};

} // namespace llvm

namespace neverc {

class ExtQualsTypeCommonBase {
  friend class ExtQuals;
  friend class QualType;
  friend class Type;

  const Type *const BaseType;

  QualType CanonicalType;

  ExtQualsTypeCommonBase(const Type *baseType, QualType canon)
      : BaseType(baseType), CanonicalType(canon) {}
};

class alignas(TypeAlignment) ExtQuals : public ExtQualsTypeCommonBase,
                                        public llvm::FoldingSetNode {
  // NOTE: changing the fast qualifiers should be straightforward as
  // long as you don't make 'const' non-fast.
  // 1. Qualifiers:
  //    a) Modify the bitmasks (Qualifiers::TQ and DeclSpec::TQ).
  //       Fast qualifiers must occupy the low-order bits.
  //    b) Update Qualifiers::FastWidth and FastMask.
  // 2. QualType:
  //    a) Update is{Volatile,Restrict}Qualified(), defined inline.
  //    b) Update remove{Volatile,Restrict}, defined near the end of
  //       this header.
  // 3. TreeContext:
  //    a) Update get{Volatile,Restrict}Type.

  Qualifiers Quals;

  ExtQuals *this_() { return this; }

public:
  ExtQuals(const Type *baseType, QualType canon, Qualifiers quals)
      : ExtQualsTypeCommonBase(baseType,
                               canon.isNull() ? QualType(this_(), 0) : canon),
        Quals(quals) {
    assert(Quals.hasNonFastQualifiers() &&
           "ExtQuals created with no fast qualifiers");
    assert(!Quals.hasFastQualifiers() &&
           "ExtQuals created with fast qualifiers");
  }

  Qualifiers getQualifiers() const { return Quals; }

  bool hasAddressSpace() const { return Quals.hasAddressSpace(); }
  LangAS getAddressSpace() const { return Quals.getAddressSpace(); }

  const Type *getBaseType() const { return BaseType; }

public:
  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, getBaseType(), Quals);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const Type *BaseType,
                      Qualifiers Quals) {
    assert(!Quals.hasFastQualifiers() && "fast qualifiers in ExtQuals hash!");
    ID.AddPointer(BaseType);
    Quals.Profile(ID);
  }
};

enum class AutoTypeKeyword {
  Auto,

  GNUAutoType
};

enum class ArraySizeModifier;
enum class ElaboratedTypeKeyword;
enum class VectorKind;

class alignas(TypeAlignment) Type : public ExtQualsTypeCommonBase {
public:
  enum TypeClass {
#define TYPE(Class, Base) Class,
#define LAST_TYPE(Class) TypeLast = Class
#define ABSTRACT_TYPE(Class, Base)
#include "neverc/Tree/TypeNodes.td.h"
  };

private:
  class TypeBitfields {
    friend class Type;
    template <class T> friend class TypePropertyCache;

    /// TypeClass bitfield - Enum that specifies what subclass this belongs to.
    LLVM_PREFERRED_TYPE(TypeClass)
    unsigned TC : 8;

    /// Store information on the type dependency.
    LLVM_PREFERRED_TYPE(TypeDependence)
    unsigned Dependence : llvm::BitWidth<TypeDependence>;

    /// True if the cache (i.e. the bitfields here starting with
    /// 'Cache') is valid.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned CacheValid : 1;

    /// Linkage of this type.
    LLVM_PREFERRED_TYPE(Linkage)
    mutable unsigned CachedLinkage : 3;

    /// Whether this type involves and local or unnamed types.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned CachedLocalOrUnnamed : 1;

    /// Whether this type comes from an AST file.
    LLVM_PREFERRED_TYPE(bool)
    mutable unsigned FromAST : 1;

    bool isCacheValid() const { return CacheValid; }

    Linkage getLinkage() const {
      assert(isCacheValid() && "getting linkage from invalid cache");
      return static_cast<Linkage>(CachedLinkage);
    }

    bool hasLocalOrUnnamedType() const {
      assert(isCacheValid() && "getting linkage from invalid cache");
      return CachedLocalOrUnnamed;
    }
  };
  enum { NumTypeBits = 8 + llvm::BitWidth<TypeDependence> + 6 };

protected:
  // These classes allow subclasses to somewhat cleanly pack bitfields
  // into Type.

  class ArrayTypeBitfields {
    friend class ArrayType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// CVR qualifiers from declarations like
    /// 'int X[static restrict 4]'. For function parameters only.
    LLVM_PREFERRED_TYPE(Qualifiers)
    unsigned IndexTypeQuals : 3;

    /// Storage class qualifiers from declarations like
    /// 'int X[static restrict 4]'. For function parameters only.
    LLVM_PREFERRED_TYPE(ArraySizeModifier)
    unsigned SizeModifier : 3;
  };
  enum { NumArrayTypeBits = NumTypeBits + 6 };

  class ConstantArrayTypeBitfields {
    friend class ConstantArrayType;

    LLVM_PREFERRED_TYPE(ArrayTypeBitfields)
    unsigned : NumArrayTypeBits;

    /// Whether we have a stored size expression.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasStoredSizeExpr : 1;
  };

  class BuiltinTypeBitfields {
    friend class BuiltinType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The kind (BuiltinType::Kind) of builtin type this is.
    static constexpr unsigned NumOfBuiltinTypeBits = 9;
    unsigned Kind : NumOfBuiltinTypeBits;
  };

  class FunctionTypeBitfields {
    friend class FunctionProtoType;
    friend class FunctionType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// Extra information which affects how the function is called, like
    /// regparm and the calling convention.
    LLVM_PREFERRED_TYPE(CallingConv)
    unsigned ExtInfo : 13;

    /// Used only by FunctionProtoType, put here to pack with the
    /// other bitfields.
    /// The qualifiers are part of FunctionProtoType because...
    ///
    /// The return type, parameter list, and cv-qualifier-seq are part of the
    /// function type.
    LLVM_PREFERRED_TYPE(Qualifiers)
    unsigned FastTypeQuals : Qualifiers::FastWidth;
    /// Whether this function has extended Qualifiers.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtQuals : 1;

    /// The number of parameters this function has, not counting '...'.
    /// According to [implimits] 8 bits should be enough here but this is
    /// somewhat easy to exceed with metaprogramming and so we would like to
    /// keep NumParams as wide as reasonably possible.
    unsigned NumParams : 16;

    LLVM_PREFERRED_TYPE(ExceptionSpecificationType)
    unsigned ExceptionSpecType : 1;

    /// Whether this function has extended parameter information.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtParameterInfos : 1;

    /// Whether this function has extra bitfields for the prototype.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasExtraBitfields : 1;

    /// Whether the function is variadic.
    LLVM_PREFERRED_TYPE(bool)
    unsigned Variadic : 1;
  };

  class TypeWithKeywordBitfields {
    friend class TypeWithKeyword;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// An ElaboratedTypeKeyword.  8 bits for efficient access.
    LLVM_PREFERRED_TYPE(ElaboratedTypeKeyword)
    unsigned Keyword : 8;
  };

  enum { NumTypeWithKeywordBits = NumTypeBits + 8 };

  class ElaboratedTypeBitfields {
    friend class ElaboratedType;

    LLVM_PREFERRED_TYPE(TypeWithKeywordBitfields)
    unsigned : NumTypeWithKeywordBits;

    /// Whether the ElaboratedType has a trailing OwnedTagDecl.
    LLVM_PREFERRED_TYPE(bool)
    unsigned HasOwnedTagDecl : 1;
  };

  class VectorTypeBitfields {
    friend class VectorType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// The kind of vector, either a generic vector type or some
    /// target-specific vector type such as for Neon.
    LLVM_PREFERRED_TYPE(VectorKind)
    unsigned VecKind : 4;
    /// The number of elements in the vector.
    uint32_t NumElements;
  };

  class AttributedTypeBitfields {
    friend class AttributedType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    LLVM_PREFERRED_TYPE(attr::Kind)
    unsigned AttrKind : 32 - NumTypeBits;
  };

  class AutoTypeBitfields {
    friend class AutoType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    LLVM_PREFERRED_TYPE(AutoTypeKeyword)
    unsigned Keyword : 2;
  };

  class TypeOfBitfields {
    friend class TypeOfType;
    friend class TypeOfExprType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;
    LLVM_PREFERRED_TYPE(bool)
    unsigned IsUnqual : 1; // If true: typeof_unqual, else: typeof
  };

  class TypedefBitfields {
    friend class TypedefType;

    LLVM_PREFERRED_TYPE(TypeBitfields)
    unsigned : NumTypeBits;

    /// True if the underlying type is different from the declared one.
    LLVM_PREFERRED_TYPE(bool)
    unsigned hasTypeDifferentFromDecl : 1;
  };

  union {
    TypeBitfields TypeBits;
    ArrayTypeBitfields ArrayTypeBits;
    ConstantArrayTypeBitfields ConstantArrayTypeBits;
    AttributedTypeBitfields AttributedTypeBits;
    AutoTypeBitfields AutoTypeBits;
    TypeOfBitfields TypeOfBits;
    TypedefBitfields TypedefBits;
    BuiltinTypeBitfields BuiltinTypeBits;
    FunctionTypeBitfields FunctionTypeBits;
    TypeWithKeywordBitfields TypeWithKeywordBits;
    ElaboratedTypeBitfields ElaboratedTypeBits;
    VectorTypeBitfields VectorTypeBits;
  };

private:
  template <class T> friend class TypePropertyCache;

  void setFromAST(bool V = true) const { TypeBits.FromAST = V; }

protected:
  friend class TreeContext;

  Type(TypeClass tc, QualType canon, TypeDependence Dependence)
      : ExtQualsTypeCommonBase(this,
                               canon.isNull() ? QualType(this_(), 0) : canon) {
    static_assert(sizeof(*this) <=
                      alignof(decltype(*this)) + sizeof(ExtQualsTypeCommonBase),
                  "changing bitfields changed sizeof(Type)!");
    static_assert(alignof(decltype(*this)) % TypeAlignment == 0,
                  "Insufficient alignment!");
    TypeBits.TC = tc;
    TypeBits.Dependence = static_cast<unsigned>(Dependence);
    TypeBits.CacheValid = false;
    TypeBits.CachedLocalOrUnnamed = false;
    TypeBits.CachedLinkage = llvm::to_underlying(Linkage::Invalid);
    TypeBits.FromAST = false;
  }

  // silence MSVC warning C4355: 'this' : used in base member initializer list
  Type *this_() { return this; }

  void setDependence(TypeDependence D) {
    TypeBits.Dependence = static_cast<unsigned>(D);
  }

  void addDependence(TypeDependence D) { setDependence(getDependence() | D); }

public:
  Type(const Type &) = delete;
  Type(Type &&) = delete;
  Type &operator=(const Type &) = delete;
  Type &operator=(Type &&) = delete;

  TypeClass getTypeClass() const { return static_cast<TypeClass>(TypeBits.TC); }

  bool isFromAST() const { return TypeBits.FromAST; }

  bool isCanonicalUnqualified() const {
    return CanonicalType == QualType(this, 0);
  }

  QualType getLocallyUnqualifiedSingleStepDesugaredType() const;

  bool isSizelessType() const;
  bool isSizelessBuiltinType() const;

  bool isSizelessVectorType() const;

  bool isSVESizelessBuiltinType() const;

  bool isSveVLSBuiltinType() const;

  QualType getSveEltType(const TreeContext &Ctx) const;

  bool isIncompleteType(NamedDecl **Def = nullptr) const;

  bool isIncompleteOrObjectType() const { return !isFunctionType(); }

  bool isObjectType() const { return !isFunctionType() && !isVoidType(); }

  bool isLiteralType(const TreeContext &Ctx) const;

  bool isStandardLayoutType() const;

  bool isBuiltinType() const;

  bool isSpecificBuiltinType(unsigned K) const;

  bool isPlaceholderType() const;
  const BuiltinType *getAsPlaceholderType() const;

  bool isSpecificPlaceholderType(unsigned K) const;

  bool isNonOverloadPlaceholderType() const;

  bool isIntegerType() const; // C99 6.2.5p17 (int, char, bool, enum)
  bool isEnumeralType() const;

  bool isBooleanType() const;
  bool isCharType() const;
  bool isWideCharType() const;
  bool isChar8Type() const;
  bool isChar16Type() const;
  bool isChar32Type() const;
  bool isAnyCharacterType() const;
  bool isIntegralType(const TreeContext &Ctx) const;

  bool isIntegralOrEnumerationType() const;

  bool isIntegralOrUnscopedEnumerationType() const;
  bool isUnscopedEnumerationType() const;

  bool isRealFloatingType() const; // C99 6.2.5p10 (float, double, long double)
  bool isComplexType() const;      // C99 6.2.5p11 (complex)
  bool isAnyComplexType() const;   // C99 6.2.5p11 (complex) + Complex Int.
  bool isFloatingType() const;     // C99 6.2.5p11 (real floating + complex)
  bool isHalfType() const;         // IEEE 754-2008 half (__fp16)
  bool isFloat16Type() const;      // C11 extension ISO/IEC TS 18661
  bool isBFloat16Type() const;
  bool isFloat128Type() const;
  bool isRealType() const;       // C99 6.2.5p17 (real floating + integer)
  bool isArithmeticType() const; // C99 6.2.5p18 (integer + floating)
  bool isVoidType() const;       // C99 6.2.5p19
  bool isScalarType() const;     // C99 6.2.5p21 (arithmetic + pointers)
  bool isAggregateType() const;

  // Type Predicates: Check to see if this type is structurally the specified
  // type, ignoring typedefs and qualifiers.
  bool isFunctionType() const;
  bool isFunctionNoProtoType() const { return getAs<FunctionNoProtoType>(); }
  bool isFunctionProtoType() const { return getAs<FunctionProtoType>(); }
  bool isPointerType() const;
  bool isAnyPointerType() const; // Any C pointer
  bool isVoidPointerType() const;
  bool isObjectPointerType() const;
  bool isFunctionPointerType() const;
  bool isArrayType() const;
  bool isConstantArrayType() const;
  bool isIncompleteArrayType() const;
  bool isVariableArrayType() const;
  bool isRecordType() const;
  bool isStructureType() const;
  bool isUnionType() const;
  bool isComplexIntegerType() const; // GCC _Complex integer type.
  bool isVectorType() const;         // GCC vector type.
  bool isExtVectorType() const;      // Extended vector type.
  bool isExtVectorBoolType() const;  // Extended vector type with bool element.
  bool isMatrixType() const;         // Matrix type.
  bool isConstantMatrixType() const; // Constant matrix type.
  bool isNullPtrType() const;        // std::nullptr_t / nullptr_t
  bool isAtomicType() const;         // C11 _Atomic()
  bool isUndeducedAutoType() const;  // auto not yet deduced
  bool isTypedefNameType() const;    // typedef or alias template

  bool isBitIntType() const; // Bit-precise integer type

  enum ScalarTypeKind {
    STK_CPointer,
    STK_Bool,
    STK_Integral,
    STK_Floating,
    STK_IntegralComplex,
    STK_FloatingComplex,
    STK_FixedPoint
  };

  ScalarTypeKind getScalarTypeKind() const;

  TypeDependence getDependence() const {
    return static_cast<TypeDependence>(TypeBits.Dependence);
  }

  bool containsErrors() const {
    return getDependence() & TypeDependence::Error;
  }

  bool isDependentType() const {
    return getDependence() & TypeDependence::Dependent;
  }

  bool isInstantiationDependentType() const {
    return getDependence() & TypeDependence::Instantiation;
  }

  bool isUndeducedType() const;

  bool isVariablyModifiedType() const {
    return getDependence() & TypeDependence::VariablyModified;
  }

  bool hasSizedVLAType() const;

  bool canDecayToPointerType() const;

  bool hasPointerRepresentation() const;

  bool hasIntegerRepresentation() const;

  bool hasSignedIntegerRepresentation() const;

  bool hasUnsignedIntegerRepresentation() const;

  bool hasFloatingRepresentation() const;

  // Type Checking Functions: Check to see if this type is structurally the
  // specified type, ignoring typedefs and qualifiers, and return a pointer to
  // the best type we can.
  const RecordType *getAsStructureType() const;
  const RecordType *getAsUnionType() const;
  const ComplexType *getAsComplexIntegerType() const; // GCC complex int type.

  RecordDecl *getAsRecordDecl() const;

  TagDecl *getAsTagDecl() const;

  DeducedType *getContainedDeducedType() const;

  AutoType *getContainedAutoType() const {
    return dyn_cast_or_null<AutoType>(getContainedDeducedType());
  }

  template <typename T> const T *getAs() const;

  template <typename T> const T *getAsAdjusted() const;

  const ArrayType *getAsArrayTypeUnsafe() const;

  template <typename T> const T *castAs() const;

  const ArrayType *castAsArrayTypeUnsafe() const;

  bool hasAttr(attr::Kind AK) const;

  const Type *getBaseElementTypeUnsafe() const;

  const Type *getArrayElementTypeNoTypeQual() const;

  const Type *getPointeeOrArrayElementType() const;

  QualType getPointeeType() const;

  const Type *getUnqualifiedDesugaredType() const;

  bool isSignedIntegerType() const;

  bool isUnsignedIntegerType() const;

  bool isSignedIntegerOrEnumerationType() const;

  bool isUnsignedIntegerOrEnumerationType() const;

  bool isFixedPointType() const;

  bool isFixedPointOrIntegerType() const;

  bool isSaturatedFixedPointType() const;

  bool isUnsaturatedFixedPointType() const;

  bool isSignedFixedPointType() const;

  bool isUnsignedFixedPointType() const;

  bool isConstantSizeType() const;

  bool isSpecifierType() const;

  Linkage getLinkage() const;

  Visibility getVisibility() const {
    return getLinkageAndVisibility().getVisibility();
  }

  bool isVisibilityExplicit() const {
    return getLinkageAndVisibility().isVisibilityExplicit();
  }

  LinkageInfo getLinkageAndVisibility() const;

  bool isLinkageValid() const;

  std::optional<NullabilityKind> getNullability() const;

  bool canHaveNullability(bool ResultIfUnknown = true) const;

  const char *getTypeClassName() const;

  QualType getCanonicalTypeInternal() const { return CanonicalType; }

  CanQualType getCanonicalTypeUnqualified() const; // in CanonicalType.h
  void dump() const;
  void dump(llvm::raw_ostream &OS, const TreeContext &Context) const;
};

template <> const TypedefType *Type::getAs() const;
template <> const AttributedType *Type::getAs() const;

// We can do canonical leaf types faster, because we don't have to
// worry about preserving child type decoration.
#define TYPE(Class, Base)
#define LEAF_TYPE(Class)                                                       \
  template <> inline const Class##Type *Type::getAs() const {                  \
    return dyn_cast<Class##Type>(CanonicalType);                               \
  }                                                                            \
  template <> inline const Class##Type *Type::castAs() const {                 \
    return cast<Class##Type>(CanonicalType);                                   \
  }
#include "neverc/Tree/TypeNodes.td.h"

class BuiltinType : public Type {
public:
  enum Kind {
// SVE Types
#define SVE_TYPE(Name, Id, SingletonId) Id,
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
// All other builtin types
#define BUILTIN_TYPE(Id, SingletonId) Id,
#define LAST_BUILTIN_TYPE(Id) LastKind = Id
#include "neverc/Tree/Type/BuiltinTypes.def"
  };

private:
  friend class TreeContext; // TreeContext creates these.

  BuiltinType(Kind K)
      : Type(Builtin, QualType(),
             K == Dependent ? TypeDependence::DependentInstantiation
                            : TypeDependence::None) {
    static_assert(Kind::LastKind <
                      (1 << BuiltinTypeBitfields::NumOfBuiltinTypeBits) &&
                  "Defined builtin type exceeds the allocated space for serial "
                  "numbering");
    BuiltinTypeBits.Kind = K;
  }

public:
  Kind getKind() const { return static_cast<Kind>(BuiltinTypeBits.Kind); }
  llvm::StringRef getName(const PrintingPolicy &Policy) const;

  const char *getNameAsCString(const PrintingPolicy &Policy) const {
    // The llvm::StringRef is null-terminated.
    llvm::StringRef str = getName(Policy);
    assert(!str.empty() && str.data()[str.size()] == '\0');
    return str.data();
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  bool isInteger() const { return getKind() >= Bool && getKind() <= Int128; }

  bool isSignedInteger() const {
    return getKind() >= Char_S && getKind() <= Int128;
  }

  bool isUnsignedInteger() const {
    return getKind() >= Bool && getKind() <= UInt128;
  }

  bool isFloatingPoint() const {
    return getKind() >= Half && getKind() <= Float128;
  }

  bool isSVEBool() const { return getKind() == Kind::SveBool; }

  bool isSVECount() const { return getKind() == Kind::SveCount; }

  static bool isPlaceholderTypeKind(Kind K) { return K >= Overload; }

  bool isPlaceholderType() const { return isPlaceholderTypeKind(getKind()); }

  bool isNonOverloadPlaceholderType() const { return getKind() > Overload; }

  static bool classof(const Type *T) { return T->getTypeClass() == Builtin; }
};

class ComplexType : public Type, public llvm::FoldingSetNode {
  friend class TreeContext; // TreeContext creates these.

  QualType ElementType;

  ComplexType(QualType Element, QualType CanonicalPtr)
      : Type(Complex, CanonicalPtr, Element->getDependence()),
        ElementType(Element) {}

public:
  QualType getElementType() const { return ElementType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getElementType()); }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Element) {
    ID.AddPointer(Element.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Complex; }
};

class ParenType : public Type, public llvm::FoldingSetNode {
  friend class TreeContext; // TreeContext creates these.

  QualType Inner;

  ParenType(QualType InnerType, QualType CanonType)
      : Type(Paren, CanonType, InnerType->getDependence()), Inner(InnerType) {}

public:
  QualType getInnerType() const { return Inner; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getInnerType(); }

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getInnerType()); }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Inner) {
    Inner.Profile(ID);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Paren; }
};

class PointerType : public Type, public llvm::FoldingSetNode {
  friend class TreeContext; // TreeContext creates these.

  QualType PointeeType;

  PointerType(QualType Pointee, QualType CanonicalPtr)
      : Type(Pointer, CanonicalPtr, Pointee->getDependence()),
        PointeeType(Pointee) {}

public:
  QualType getPointeeType() const { return PointeeType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getPointeeType()); }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Pointee) {
    ID.AddPointer(Pointee.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Pointer; }
};

class AdjustedType : public Type, public llvm::FoldingSetNode {
  QualType OriginalTy;
  QualType AdjustedTy;

protected:
  friend class TreeContext; // TreeContext creates these.

  AdjustedType(TypeClass TC, QualType OriginalTy, QualType AdjustedTy,
               QualType CanonicalPtr)
      : Type(TC, CanonicalPtr, OriginalTy->getDependence()),
        OriginalTy(OriginalTy), AdjustedTy(AdjustedTy) {}

public:
  QualType getOriginalType() const { return OriginalTy; }
  QualType getAdjustedType() const { return AdjustedTy; }

  bool isSugared() const { return true; }
  QualType desugar() const { return AdjustedTy; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, OriginalTy, AdjustedTy);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Orig, QualType New) {
    ID.AddPointer(Orig.getAsOpaquePtr());
    ID.AddPointer(New.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Adjusted || T->getTypeClass() == Decayed;
  }
};

class DecayedType : public AdjustedType {
  friend class TreeContext; // TreeContext creates these.

  inline DecayedType(QualType OriginalType, QualType Decayed,
                     QualType Canonical);

public:
  QualType getDecayedType() const { return getAdjustedType(); }

  inline QualType getPointeeType() const;

  static bool classof(const Type *T) { return T->getTypeClass() == Decayed; }
};

enum class ArraySizeModifier { Normal, Static, Star };

class ArrayType : public Type, public llvm::FoldingSetNode {
private:
  QualType ElementType;

protected:
  friend class TreeContext; // TreeContext creates these.

  ArrayType(TypeClass tc, QualType et, QualType can, ArraySizeModifier sm,
            unsigned tq, const Expr *sz = nullptr);

public:
  QualType getElementType() const { return ElementType; }

  ArraySizeModifier getSizeModifier() const {
    return ArraySizeModifier(ArrayTypeBits.SizeModifier);
  }

  Qualifiers getIndexTypeQualifiers() const {
    return Qualifiers::fromCVRMask(getIndexTypeCVRQualifiers());
  }

  unsigned getIndexTypeCVRQualifiers() const {
    return ArrayTypeBits.IndexTypeQuals;
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantArray ||
           T->getTypeClass() == VariableArray ||
           T->getTypeClass() == IncompleteArray;
  }
};

class ConstantArrayType final
    : public ArrayType,
      private llvm::TrailingObjects<ConstantArrayType, const Expr *> {
  friend class TreeContext; // TreeContext creates these.
  friend TrailingObjects;

  llvm::APInt Size; // Allows us to unique the type.

  ConstantArrayType(QualType et, QualType can, const llvm::APInt &size,
                    const Expr *sz, ArraySizeModifier sm, unsigned tq)
      : ArrayType(ConstantArray, et, can, sm, tq, sz), Size(size) {
    ConstantArrayTypeBits.HasStoredSizeExpr = sz != nullptr;
    if (ConstantArrayTypeBits.HasStoredSizeExpr) {
      assert(!can.isNull() && "canonical constant array should not have size");
      *getTrailingObjects<const Expr *>() = sz;
    }
  }

  unsigned numTrailingObjects(OverloadToken<const Expr *>) const {
    return ConstantArrayTypeBits.HasStoredSizeExpr;
  }

public:
  const llvm::APInt &getSize() const { return Size; }
  const Expr *getSizeExpr() const {
    return ConstantArrayTypeBits.HasStoredSizeExpr
               ? *getTrailingObjects<const Expr *>()
               : nullptr;
  }
  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  // an array with the given element type and number of elements.
  static unsigned getNumAddressingBits(const TreeContext &Context,
                                       QualType ElementType,
                                       const llvm::APInt &NumElements);

  unsigned getNumAddressingBits(const TreeContext &Context) const;

  static unsigned getMaxSizeBits(const TreeContext &Context);

  void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Ctx) {
    Profile(ID, Ctx, getElementType(), getSize(), getSizeExpr(),
            getSizeModifier(), getIndexTypeCVRQualifiers());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Ctx,
                      QualType ET, const llvm::APInt &ArraySize,
                      const Expr *SizeExpr, ArraySizeModifier SizeMod,
                      unsigned TypeQuals);

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantArray;
  }
};

class IncompleteArrayType : public ArrayType {
  friend class TreeContext; // TreeContext creates these.

  IncompleteArrayType(QualType et, QualType can, ArraySizeModifier sm,
                      unsigned tq)
      : ArrayType(IncompleteArray, et, can, sm, tq) {}

public:
  friend class StmtIteratorBase;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == IncompleteArray;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getSizeModifier(),
            getIndexTypeCVRQualifiers());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ET,
                      ArraySizeModifier SizeMod, unsigned TypeQuals) {
    ID.AddPointer(ET.getAsOpaquePtr());
    ID.AddInteger(llvm::to_underlying(SizeMod));
    ID.AddInteger(TypeQuals);
  }
};

class VariableArrayType : public ArrayType {
  friend class TreeContext; // TreeContext creates these.

  Stmt *SizeExpr;

  SourceRange Brackets;

  VariableArrayType(QualType et, QualType can, Expr *e, ArraySizeModifier sm,
                    unsigned tq, SourceRange brackets)
      : ArrayType(VariableArray, et, can, sm, tq, e), SizeExpr((Stmt *)e),
        Brackets(brackets) {}

public:
  friend class StmtIteratorBase;

  Expr *getSizeExpr() const {
    // We use C-style casts instead of cast<> here because we do not wish
    // to have a dependency of Type.h on Stmt.h/Expr.h.
    return (Expr *)SizeExpr;
  }

  SourceRange getBracketsRange() const { return Brackets; }
  SourceLocation getLBracketLoc() const { return Brackets.getBegin(); }
  SourceLocation getRBracketLoc() const { return Brackets.getEnd(); }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == VariableArray;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    llvm_unreachable("Cannot unique VariableArrayTypes.");
  }
};

enum class VectorKind {
  Generic,

  Neon,

  NeonPoly,

  SveFixedLengthData,

  SveFixedLengthPredicate,

};

class VectorType : public Type, public llvm::FoldingSetNode {
protected:
  friend class TreeContext; // TreeContext creates these.

  QualType ElementType;

  VectorType(QualType vecType, unsigned nElements, QualType canonType,
             VectorKind vecKind);

  VectorType(TypeClass tc, QualType vecType, unsigned nElements,
             QualType canonType, VectorKind vecKind);

public:
  QualType getElementType() const { return ElementType; }
  unsigned getNumElements() const { return VectorTypeBits.NumElements; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  VectorKind getVectorKind() const {
    return VectorKind(VectorTypeBits.VecKind);
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getNumElements(), getTypeClass(),
            getVectorKind());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ElementType,
                      unsigned NumElements, TypeClass TypeClass,
                      VectorKind VecKind) {
    ID.AddPointer(ElementType.getAsOpaquePtr());
    ID.AddInteger(NumElements);
    ID.AddInteger(TypeClass);
    ID.AddInteger(llvm::to_underlying(VecKind));
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == Vector || T->getTypeClass() == ExtVector;
  }
};

////
class ExtVectorType : public VectorType {
  friend class TreeContext; // TreeContext creates these.

  ExtVectorType(QualType vecType, unsigned nElements, QualType canonType)
      : VectorType(ExtVector, vecType, nElements, canonType,
                   VectorKind::Generic) {}

public:
  static int getPointAccessorIdx(char c) {
    switch (c) {
    default:
      return -1;
    case 'x':
    case 'r':
      return 0;
    case 'y':
    case 'g':
      return 1;
    case 'z':
    case 'b':
      return 2;
    case 'w':
    case 'a':
      return 3;
    }
  }

  static int getNumericAccessorIdx(char c) {
    switch (c) {
    default:
      return -1;
    case '0':
      return 0;
    case '1':
      return 1;
    case '2':
      return 2;
    case '3':
      return 3;
    case '4':
      return 4;
    case '5':
      return 5;
    case '6':
      return 6;
    case '7':
      return 7;
    case '8':
      return 8;
    case '9':
      return 9;
    case 'A':
    case 'a':
      return 10;
    case 'B':
    case 'b':
      return 11;
    case 'C':
    case 'c':
      return 12;
    case 'D':
    case 'd':
      return 13;
    case 'E':
    case 'e':
      return 14;
    case 'F':
    case 'f':
      return 15;
    }
  }

  static int getAccessorIdx(char c, bool isNumericAccessor) {
    if (isNumericAccessor)
      return getNumericAccessorIdx(c);
    else
      return getPointAccessorIdx(c);
  }

  bool isAccessorWithinNumElements(char c, bool isNumericAccessor) const {
    if (int idx = getAccessorIdx(c, isNumericAccessor) + 1)
      return unsigned(idx - 1) < getNumElements();
    return false;
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) { return T->getTypeClass() == ExtVector; }
};

class MatrixType : public Type, public llvm::FoldingSetNode {
protected:
  friend class TreeContext;

  QualType ElementType;

  MatrixType(QualType ElementTy, QualType CanonElementTy);

  MatrixType(TypeClass TypeClass, QualType ElementTy, QualType CanonElementTy,
             const Expr *RowExpr = nullptr, const Expr *ColumnExpr = nullptr);

public:
  QualType getElementType() const { return ElementType; }

  static bool isValidElementType(QualType T) {
    return T->isDependentType() ||
           (T->isRealType() && !T->isBooleanType() && !T->isEnumeralType());
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantMatrix;
  }
};

class ConstantMatrixType final : public MatrixType {
protected:
  friend class TreeContext;

  unsigned NumRows;
  unsigned NumColumns;

  static constexpr unsigned MaxElementsPerDimension = (1 << 20) - 1;

  ConstantMatrixType(QualType MatrixElementType, unsigned NRows,
                     unsigned NColumns, QualType CanonElementType);

  ConstantMatrixType(TypeClass typeClass, QualType MatrixType, unsigned NRows,
                     unsigned NColumns, QualType CanonElementType);

public:
  unsigned getNumRows() const { return NumRows; }

  unsigned getNumColumns() const { return NumColumns; }

  unsigned getNumElementsFlattened() const {
    return getNumRows() * getNumColumns();
  }

  static constexpr bool isDimensionValid(size_t NumElements) {
    return NumElements > 0 && NumElements <= MaxElementsPerDimension;
  }

  static constexpr unsigned getMaxElementsPerDimension() {
    return MaxElementsPerDimension;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getElementType(), getNumRows(), getNumColumns(),
            getTypeClass());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ElementType,
                      unsigned NumRows, unsigned NumColumns,
                      TypeClass TypeClass) {
    ID.AddPointer(ElementType.getAsOpaquePtr());
    ID.AddInteger(NumRows);
    ID.AddInteger(NumColumns);
    ID.AddInteger(TypeClass);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == ConstantMatrix;
  }
};

class FunctionType : public Type {
  // The type returned by the function.
  QualType ResultType;

public:
  class ExtParameterInfo {
    enum {
      HasPassObjSize = 0x20,
      IsNoEscape = 0x40,
    };
    unsigned char Data = 0;

  public:
    ExtParameterInfo() = default;

    bool hasPassObjectSize() const { return Data & HasPassObjSize; }
    ExtParameterInfo withHasPassObjectSize() const {
      ExtParameterInfo Copy = *this;
      Copy.Data |= HasPassObjSize;
      return Copy;
    }

    bool isNoEscape() const { return Data & IsNoEscape; }
    ExtParameterInfo withIsNoEscape(bool NoEscape) const {
      ExtParameterInfo Copy = *this;
      if (NoEscape)
        Copy.Data |= IsNoEscape;
      else
        Copy.Data &= ~IsNoEscape;
      return Copy;
    }

    unsigned char getOpaqueValue() const { return Data; }
    static ExtParameterInfo getFromOpaqueValue(unsigned char data) {
      ExtParameterInfo result;
      result.Data = data;
      return result;
    }

    friend bool operator==(ExtParameterInfo lhs, ExtParameterInfo rhs) {
      return lhs.Data == rhs.Data;
    }

    friend bool operator!=(ExtParameterInfo lhs, ExtParameterInfo rhs) {
      return lhs.Data != rhs.Data;
    }
  };

  // If you add a field (say Foo), other than the obvious places (both,
  // constructors, compile failures), what you need to update is
  // * Operator==
  // * getFoo
  // * withFoo
  // * functionType. Add Foo, getFoo.
  // * TreeContext::getFooType
  // * TreeContext::mergeFunctionTypes
  // * FunctionNoProtoType::Profile
  // * FunctionProtoType::Profile
  // * TypePrinter::PrintFunctionProto
  // * AST read and write
  // * Codegen
  class ExtInfo {
    friend class FunctionType;

    // Feel free to rearrange or add bits, but if you go over 16, you'll need to
    // adjust the Bits field below, and if you add bits, you'll need to adjust
    // Type::FunctionTypeBitfields::ExtInfo as well.

    // |  CC  |noreturn|produces|nocallersavedregs|regparm|nocfcheck|cmsenscall|
    // |0 .. 4|   5    |    6   |       7         |8 .. 10|    11   |    12    |
    //
    // regparm is either 0 (no regparm attribute) or the regparm value+1.
    enum { CallConvMask = 0x1F };
    enum { NoReturnMask = 0x20 };
    enum { ProducesResultMask = 0x40 };
    enum { NoCallerSavedRegsMask = 0x80 };
    enum { RegParmMask = 0x700, RegParmOffset = 8 };
    enum { NoCfCheckMask = 0x800 };
    enum { CmseNSCallMask = 0x1000 };
    uint16_t Bits = CC_C;

    ExtInfo(unsigned Bits) : Bits(static_cast<uint16_t>(Bits)) {}

  public:
    // Constructor with no defaults. Use this when you know that you
    // have all the elements (when reading an AST file for example).
    ExtInfo(bool noReturn, bool hasRegParm, unsigned regParm, CallingConv cc,
            bool producesResult, bool noCallerSavedRegs, bool NoCfCheck,
            bool cmseNSCall) {
      assert((!hasRegParm || regParm < 7) && "Invalid regparm value");
      Bits = ((unsigned)cc) | (noReturn ? NoReturnMask : 0) |
             (producesResult ? ProducesResultMask : 0) |
             (noCallerSavedRegs ? NoCallerSavedRegsMask : 0) |
             (hasRegParm ? ((regParm + 1) << RegParmOffset) : 0) |
             (NoCfCheck ? NoCfCheckMask : 0) |
             (cmseNSCall ? CmseNSCallMask : 0);
    }

    // Constructor with all defaults. Use when for example creating a
    // function known to use defaults.
    ExtInfo() = default;

    // Constructor with just the calling convention, which is an important part
    // of the canonical type.
    ExtInfo(CallingConv CC) : Bits(CC) {}

    bool getNoReturn() const { return Bits & NoReturnMask; }
    bool getProducesResult() const { return Bits & ProducesResultMask; }
    bool getCmseNSCall() const { return Bits & CmseNSCallMask; }
    bool getNoCallerSavedRegs() const { return Bits & NoCallerSavedRegsMask; }
    bool getNoCfCheck() const { return Bits & NoCfCheckMask; }
    bool getHasRegParm() const {
      return ((Bits & RegParmMask) >> RegParmOffset) != 0;
    }

    unsigned getRegParm() const {
      unsigned RegParm = (Bits & RegParmMask) >> RegParmOffset;
      if (RegParm > 0)
        --RegParm;
      return RegParm;
    }

    CallingConv getCC() const { return CallingConv(Bits & CallConvMask); }

    bool operator==(ExtInfo Other) const { return Bits == Other.Bits; }
    bool operator!=(ExtInfo Other) const { return Bits != Other.Bits; }

    // Note that we don't have setters. That is by design, use
    // the following with methods instead of mutating these objects.

    ExtInfo withNoReturn(bool noReturn) const {
      if (noReturn)
        return ExtInfo(Bits | NoReturnMask);
      else
        return ExtInfo(Bits & ~NoReturnMask);
    }

    ExtInfo withProducesResult(bool producesResult) const {
      if (producesResult)
        return ExtInfo(Bits | ProducesResultMask);
      else
        return ExtInfo(Bits & ~ProducesResultMask);
    }

    ExtInfo withCmseNSCall(bool cmseNSCall) const {
      if (cmseNSCall)
        return ExtInfo(Bits | CmseNSCallMask);
      else
        return ExtInfo(Bits & ~CmseNSCallMask);
    }

    ExtInfo withNoCallerSavedRegs(bool noCallerSavedRegs) const {
      if (noCallerSavedRegs)
        return ExtInfo(Bits | NoCallerSavedRegsMask);
      else
        return ExtInfo(Bits & ~NoCallerSavedRegsMask);
    }

    ExtInfo withNoCfCheck(bool noCfCheck) const {
      if (noCfCheck)
        return ExtInfo(Bits | NoCfCheckMask);
      else
        return ExtInfo(Bits & ~NoCfCheckMask);
    }

    ExtInfo withRegParm(unsigned RegParm) const {
      assert(RegParm < 7 && "Invalid regparm value");
      return ExtInfo((Bits & ~RegParmMask) | ((RegParm + 1) << RegParmOffset));
    }

    ExtInfo withCallingConv(CallingConv cc) const {
      return ExtInfo((Bits & ~CallConvMask) | (unsigned)cc);
    }

    void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddInteger(Bits); }
  };

  enum AArch64SMETypeAttributes : unsigned {
    SME_NormalFunction = 0,
    SME_PStateSMEnabledMask = 1 << 0,
    SME_PStateSMCompatibleMask = 1 << 1,
    SME_PStateZASharedMask = 1 << 2,
    SME_PStateZAPreservedMask = 1 << 3,
    SME_AttributeMask = 0b111'111 // We only support maximum 6 bits because of
                                  // the bitmask in FunctionTypeExtraBitfields.
  };

  struct alignas(void *) FunctionTypeExtraBitfields {
    unsigned AArch64SMEAttributes : 6;
    FunctionTypeExtraBitfields() : AArch64SMEAttributes(SME_NormalFunction) {}
  };

protected:
  FunctionType(TypeClass tc, QualType res, QualType Canonical,
               TypeDependence Dependence, ExtInfo Info)
      : Type(tc, Canonical, Dependence), ResultType(res) {
    FunctionTypeBits.ExtInfo = Info.Bits;
  }

  Qualifiers getFastTypeQuals() const {
    if (isFunctionProtoType())
      return Qualifiers::fromFastMask(FunctionTypeBits.FastTypeQuals);

    return Qualifiers();
  }

public:
  QualType getReturnType() const { return ResultType; }

  bool getHasRegParm() const { return getExtInfo().getHasRegParm(); }
  unsigned getRegParmType() const { return getExtInfo().getRegParm(); }

  bool getNoReturnAttr() const { return getExtInfo().getNoReturn(); }

  bool getCmseNSCallAttr() const { return getExtInfo().getCmseNSCall(); }
  CallingConv getCallConv() const { return getExtInfo().getCC(); }
  ExtInfo getExtInfo() const { return ExtInfo(FunctionTypeBits.ExtInfo); }

  static_assert((~Qualifiers::FastMask & Qualifiers::CVRMask) == 0,
                "Const, volatile and restrict are assumed to be a subset of "
                "the fast qualifiers.");

  bool isConst() const { return getFastTypeQuals().hasConst(); }
  bool isVolatile() const { return getFastTypeQuals().hasVolatile(); }
  bool isRestrict() const { return getFastTypeQuals().hasRestrict(); }

  QualType getCallResultType(const TreeContext &Context) const {
    return getReturnType().getNonLValueExprType(Context);
  }

  static llvm::StringRef getNameForCallConv(CallingConv CC);

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionNoProto ||
           T->getTypeClass() == FunctionProto;
  }
};

class FunctionNoProtoType : public FunctionType, public llvm::FoldingSetNode {
  friend class TreeContext; // TreeContext creates these.

  FunctionNoProtoType(QualType Result, QualType Canonical, ExtInfo Info)
      : FunctionType(FunctionNoProto, Result, Canonical,
                     Result->getDependence() &
                         ~TypeDependence::DependentInstantiation,
                     Info) {}

public:
  // No additional state past what FunctionType provides.

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getReturnType(), getExtInfo());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType ResultType,
                      ExtInfo Info) {
    Info.Profile(ID);
    ID.AddPointer(ResultType.getAsOpaquePtr());
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionNoProto;
  }
};

class FunctionProtoType final
    : public FunctionType,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<FunctionProtoType, QualType, SourceLocation,
                                    FunctionType::FunctionTypeExtraBitfields,
                                    FunctionType::ExtParameterInfo,
                                    Qualifiers> {
  friend class TreeContext;
  friend TrailingObjects;
  //
  // The optional FunctionTypeExtraBitfields has to be before the data
  // related to the exception specification since it contains the number
  // of exception types.
  //
  // We put the ExtParameterInfos last.  If all were equal, it would make
  // more sense to put these before the exception specification, because
  // it's much easier to skip past them compared to the elaborate switch
  // required to skip the exception specification.  However, all is not
  // equal; ExtParameterInfos are used to model very uncommon features,
  // and it's better not to burden the more common paths.

public:
  struct ExceptionSpecInfo {
    ExceptionSpecificationType Type = EST_None;

    ExceptionSpecInfo() = default;
    ExceptionSpecInfo(ExceptionSpecificationType EST) : Type(EST) {}
  };

  struct ExtProtoInfo {
    FunctionType::ExtInfo ExtInfo;
    unsigned Variadic : 1;
    unsigned AArch64SMEAttributes : 6;
    Qualifiers TypeQuals;
    ExceptionSpecInfo ExceptionSpec;
    const ExtParameterInfo *ExtParameterInfos = nullptr;
    SourceLocation EllipsisLoc;

    ExtProtoInfo()
        : Variadic(false), AArch64SMEAttributes(SME_NormalFunction) {}

    ExtProtoInfo(CallingConv CC)
        : ExtInfo(CC), Variadic(false),
          AArch64SMEAttributes(SME_NormalFunction) {}

    ExtProtoInfo withExceptionSpec(const ExceptionSpecInfo &ESI) {
      ExtProtoInfo Result(*this);
      Result.ExceptionSpec = ESI;
      return Result;
    }

    bool requiresFunctionProtoTypeExtraBitfields() const {
      return AArch64SMEAttributes != SME_NormalFunction;
    }

    void setArmSMEAttribute(AArch64SMETypeAttributes Kind, bool Enable = true) {
      if (Enable)
        AArch64SMEAttributes |= Kind;
      else
        AArch64SMEAttributes &= ~Kind;
    }
  };

private:
  unsigned numTrailingObjects(OverloadToken<QualType>) const {
    return getNumParams();
  }

  unsigned numTrailingObjects(OverloadToken<SourceLocation>) const {
    return isVariadic();
  }

  unsigned numTrailingObjects(OverloadToken<FunctionTypeExtraBitfields>) const {
    return hasExtraBitfields();
  }

  unsigned numTrailingObjects(OverloadToken<ExtParameterInfo>) const {
    return hasExtParameterInfos() ? getNumParams() : 0;
  }

  FunctionProtoType(QualType result, llvm::ArrayRef<QualType> params,
                    QualType canonical, const ExtProtoInfo &epi);

  bool hasExtraBitfields() const { return FunctionTypeBits.HasExtraBitfields; }

  bool hasExtQualifiers() const { return FunctionTypeBits.HasExtQuals; }

public:
  unsigned getNumParams() const { return FunctionTypeBits.NumParams; }

  QualType getParamType(unsigned i) const {
    assert(i < getNumParams() && "invalid parameter index");
    return param_type_begin()[i];
  }

  llvm::ArrayRef<QualType> getParamTypes() const {
    return llvm::ArrayRef(param_type_begin(), param_type_end());
  }

  ExtProtoInfo getExtProtoInfo() const {
    ExtProtoInfo EPI;
    EPI.ExtInfo = getExtInfo();
    EPI.Variadic = isVariadic();
    EPI.EllipsisLoc = getEllipsisLoc();
    EPI.ExceptionSpec = getExceptionSpecInfo();
    EPI.TypeQuals = getMethodQuals();
    EPI.ExtParameterInfos = getExtParameterInfosOrNull();
    EPI.AArch64SMEAttributes = getAArch64SMEAttributes();
    return EPI;
  }

  ExceptionSpecificationType getExceptionSpecType() const {
    return static_cast<ExceptionSpecificationType>(
        FunctionTypeBits.ExceptionSpecType);
  }

  bool hasExceptionSpec() const { return getExceptionSpecType() != EST_None; }

  ExceptionSpecInfo getExceptionSpecInfo() const {
    ExceptionSpecInfo Result;
    Result.Type = getExceptionSpecType();
    return Result;
  }

  bool isNothrow() const { return getExceptionSpecType() == EST_NoThrow; }

  bool isVariadic() const { return FunctionTypeBits.Variadic; }

  SourceLocation getEllipsisLoc() const {
    return isVariadic() ? *getTrailingObjects<SourceLocation>()
                        : SourceLocation();
  }

  Qualifiers getMethodQuals() const {
    if (hasExtQualifiers())
      return *getTrailingObjects<Qualifiers>();
    else
      return getFastTypeQuals();
  }

  using param_type_iterator = const QualType *;

  llvm::ArrayRef<QualType> param_types() const {
    return llvm::ArrayRef(param_type_begin(), param_type_end());
  }

  param_type_iterator param_type_begin() const {
    return getTrailingObjects<QualType>();
  }

  param_type_iterator param_type_end() const {
    return param_type_begin() + getNumParams();
  }

  bool hasExtParameterInfos() const {
    return FunctionTypeBits.HasExtParameterInfos;
  }

  llvm::ArrayRef<ExtParameterInfo> getExtParameterInfos() const {
    assert(hasExtParameterInfos());
    return llvm::ArrayRef<ExtParameterInfo>(
        getTrailingObjects<ExtParameterInfo>(), getNumParams());
  }

  const ExtParameterInfo *getExtParameterInfosOrNull() const {
    if (!hasExtParameterInfos())
      return nullptr;
    return getTrailingObjects<ExtParameterInfo>();
  }

  unsigned getAArch64SMEAttributes() const {
    if (!hasExtraBitfields())
      return SME_NormalFunction;
    return getTrailingObjects<FunctionTypeExtraBitfields>()
        ->AArch64SMEAttributes;
  }

  ExtParameterInfo getExtParameterInfo(unsigned I) const {
    assert(I < getNumParams() && "parameter index out of range");
    if (hasExtParameterInfos())
      return getTrailingObjects<ExtParameterInfo>()[I];
    return ExtParameterInfo();
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void printExceptionSpecification(llvm::raw_ostream &OS,
                                   const PrintingPolicy &Policy) const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == FunctionProto;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Ctx);
  static void Profile(llvm::FoldingSetNodeID &ID, QualType Result,
                      param_type_iterator ArgTys, unsigned NumArgs,
                      const ExtProtoInfo &EPI, const TreeContext &Context,
                      bool Canonical);
};

class TypedefType final : public Type,
                          public llvm::FoldingSetNode,
                          private llvm::TrailingObjects<TypedefType, QualType> {
  TypedefNameDecl *Decl;
  friend class TreeContext; // TreeContext creates these.
  friend TrailingObjects;

  TypedefType(TypeClass tc, const TypedefNameDecl *D, QualType underlying,
              QualType can);

public:
  TypedefNameDecl *getDecl() const { return Decl; }

  bool isSugared() const { return true; }

  // This always has the 'same' type as declared, but not necessarily identical.
  QualType desugar() const;

  // Internal helper, for debugging purposes.
  bool typeMatchesDecl() const { return !TypedefBits.hasTypeDifferentFromDecl; }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, Decl, typeMatchesDecl() ? QualType() : desugar());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, const TypedefNameDecl *Decl,
                      QualType Underlying) {
    ID.AddPointer(Decl);
    if (!Underlying.isNull())
      Underlying.Profile(ID);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Typedef; }
};

class MacroQualifiedType : public Type {
  friend class TreeContext; // TreeContext creates these.

  QualType UnderlyingTy;
  const IdentifierInfo *MacroII;

  MacroQualifiedType(QualType UnderlyingTy, QualType CanonTy,
                     const IdentifierInfo *MacroII)
      : Type(MacroQualified, CanonTy, UnderlyingTy->getDependence()),
        UnderlyingTy(UnderlyingTy), MacroII(MacroII) {
    assert(isa<AttributedType>(UnderlyingTy) &&
           "Expected a macro qualified type to only wrap attributed types.");
  }

public:
  const IdentifierInfo *getMacroIdentifier() const { return MacroII; }
  QualType getUnderlyingType() const { return UnderlyingTy; }

  QualType getModifiedType() const;

  bool isSugared() const { return true; }
  QualType desugar() const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == MacroQualified;
  }
};

class TypeOfExprType : public Type {
  Expr *TOExpr;

protected:
  friend class TreeContext; // TreeContext creates these.

  TypeOfExprType(Expr *E, TypeOfKind Kind, QualType Can = QualType());

public:
  Expr *getUnderlyingExpr() const { return TOExpr; }

  TypeOfKind getKind() const {
    return TypeOfBits.IsUnqual ? TypeOfKind::Unqualified
                               : TypeOfKind::Qualified;
  }

  QualType desugar() const;

  bool isSugared() const;

  static bool classof(const Type *T) { return T->getTypeClass() == TypeOfExpr; }
};

class DependentTypeOfExprType : public TypeOfExprType,
                                public llvm::FoldingSetNode {
public:
  DependentTypeOfExprType(Expr *E, TypeOfKind Kind) : TypeOfExprType(E, Kind) {}

  void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context) {
    Profile(ID, Context, getUnderlyingExpr(),
            getKind() == TypeOfKind::Unqualified);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context,
                      Expr *E, bool IsUnqual);
};

class TypeOfType : public Type {
  friend class TreeContext; // TreeContext creates these.

  QualType TOType;

  TypeOfType(QualType T, QualType Can, TypeOfKind Kind)
      : Type(TypeOf,
             Kind == TypeOfKind::Unqualified ? Can.getAtomicUnqualifiedType()
                                             : Can,
             T->getDependence()),
        TOType(T) {
    TypeOfBits.IsUnqual = Kind == TypeOfKind::Unqualified;
  }

public:
  QualType getUnmodifiedType() const { return TOType; }

  QualType desugar() const {
    QualType QT = getUnmodifiedType();
    return TypeOfBits.IsUnqual ? QT.getAtomicUnqualifiedType() : QT;
  }

  bool isSugared() const { return true; }

  TypeOfKind getKind() const {
    return TypeOfBits.IsUnqual ? TypeOfKind::Unqualified
                               : TypeOfKind::Qualified;
  }

  static bool classof(const Type *T) { return T->getTypeClass() == TypeOf; }
};

class TagType : public Type {

  TagDecl *decl;

protected:
  TagType(TypeClass TC, const TagDecl *D, QualType can);

public:
  TagDecl *getDecl() const;

  bool isBeingDefined() const;

  static bool classof(const Type *T) {
    return T->getTypeClass() == Enum || T->getTypeClass() == Record;
  }
};

class RecordType : public TagType {
protected:
  friend class TreeContext; // TreeContext creates these.

  explicit RecordType(const RecordDecl *D)
      : TagType(Record, reinterpret_cast<const TagDecl *>(D), QualType()) {}
  explicit RecordType(TypeClass TC, RecordDecl *D)
      : TagType(TC, reinterpret_cast<const TagDecl *>(D), QualType()) {}

public:
  RecordDecl *getDecl() const {
    return reinterpret_cast<RecordDecl *>(TagType::getDecl());
  }

  bool hasConstFields() const;

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) { return T->getTypeClass() == Record; }
};

class EnumType : public TagType {
  friend class TreeContext; // TreeContext creates these.

  explicit EnumType(const EnumDecl *D)
      : TagType(Enum, reinterpret_cast<const TagDecl *>(D), QualType()) {}

public:
  EnumDecl *getDecl() const {
    return reinterpret_cast<EnumDecl *>(TagType::getDecl());
  }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  static bool classof(const Type *T) { return T->getTypeClass() == Enum; }
};

class AttributedType : public Type, public llvm::FoldingSetNode {
public:
  using Kind = attr::Kind;

private:
  friend class TreeContext; // TreeContext creates these

  QualType ModifiedType;
  QualType EquivalentType;

  AttributedType(QualType canon, attr::Kind attrKind, QualType modified,
                 QualType equivalent)
      : Type(Attributed, canon, equivalent->getDependence()),
        ModifiedType(modified), EquivalentType(equivalent) {
    AttributedTypeBits.AttrKind = attrKind;
  }

public:
  Kind getAttrKind() const {
    return static_cast<Kind>(AttributedTypeBits.AttrKind);
  }

  QualType getModifiedType() const { return ModifiedType; }
  QualType getEquivalentType() const { return EquivalentType; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getEquivalentType(); }

  bool isQualifier() const;

  bool isMSTypeSpec() const;

  bool isCallingConv() const;

  std::optional<NullabilityKind> getImmediateNullability() const;

  static Kind getNullabilityAttrKind(NullabilityKind kind) {
    switch (kind) {
    case NullabilityKind::NonNull:
      return attr::TypeNonNull;

    case NullabilityKind::Nullable:
      return attr::TypeNullable;

    case NullabilityKind::Unspecified:
      return attr::TypeNullUnspecified;
    }
    llvm_unreachable("Unknown nullability kind.");
  }

  static std::optional<NullabilityKind> stripOuterNullability(QualType &T);

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getAttrKind(), ModifiedType, EquivalentType);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, Kind attrKind,
                      QualType modified, QualType equivalent) {
    ID.AddInteger(attrKind);
    ID.AddPointer(modified.getAsOpaquePtr());
    ID.AddPointer(equivalent.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Attributed; }
};

class BTFTagAttributedType : public Type, public llvm::FoldingSetNode {
private:
  friend class TreeContext; // TreeContext creates these

  QualType WrappedType;
  const BTFTypeTagAttr *BTFAttr;

  BTFTagAttributedType(QualType Canon, QualType Wrapped,
                       const BTFTypeTagAttr *BTFAttr)
      : Type(BTFTagAttributed, Canon, Wrapped->getDependence()),
        WrappedType(Wrapped), BTFAttr(BTFAttr) {}

public:
  QualType getWrappedType() const { return WrappedType; }
  const BTFTypeTagAttr *getAttr() const { return BTFAttr; }

  bool isSugared() const { return true; }
  QualType desugar() const { return getWrappedType(); }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, WrappedType, BTFAttr);
  }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType Wrapped,
                      const BTFTypeTagAttr *BTFAttr) {
    ID.AddPointer(Wrapped.getAsOpaquePtr());
    ID.AddPointer(BTFAttr);
  }

  static bool classof(const Type *T) {
    return T->getTypeClass() == BTFTagAttributed;
  }
};

class DeducedType : public Type {
  QualType DeducedAsType;

protected:
  DeducedType(TypeClass TC, QualType DeducedAsType,
              TypeDependence ExtraDependence, QualType Canon)
      : Type(TC, Canon,
             ExtraDependence | (DeducedAsType.isNull()
                                    ? TypeDependence::None
                                    : DeducedAsType->getDependence() &
                                          ~TypeDependence::VariablyModified)),
        DeducedAsType(DeducedAsType) {}

public:
  bool isSugared() const { return !DeducedAsType.isNull(); }
  QualType desugar() const {
    return isSugared() ? DeducedAsType : QualType(this, 0);
  }

  QualType getDeducedType() const { return DeducedAsType; }
  bool isDeduced() const {
    return !DeducedAsType.isNull() || isDependentType();
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Auto; }
};

class AutoType : public DeducedType, public llvm::FoldingSetNode {
  friend class TreeContext;

  AutoType(QualType DeducedAsType, AutoTypeKeyword Keyword,
           TypeDependence ExtraDependence, QualType Canon);

public:
  bool isGNUAutoType() const {
    return getKeyword() == AutoTypeKeyword::GNUAutoType;
  }

  AutoTypeKeyword getKeyword() const {
    return (AutoTypeKeyword)AutoTypeBits.Keyword;
  }

  void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context);
  static void Profile(llvm::FoldingSetNodeID &ID, const TreeContext &Context,
                      QualType Deduced, AutoTypeKeyword Keyword,
                      bool IsDependent);

  static bool classof(const Type *T) { return T->getTypeClass() == Auto; }
};

enum class ElaboratedTypeKeyword {
  Struct,

  Union,

  Enum,

  None
};

enum class TagTypeKind {
  Struct,

  Union,

  Enum
};

class TypeWithKeyword : public Type {
protected:
  TypeWithKeyword(ElaboratedTypeKeyword Keyword, TypeClass tc,
                  QualType Canonical, TypeDependence Dependence)
      : Type(tc, Canonical, Dependence) {
    TypeWithKeywordBits.Keyword = llvm::to_underlying(Keyword);
  }

public:
  ElaboratedTypeKeyword getKeyword() const {
    return static_cast<ElaboratedTypeKeyword>(TypeWithKeywordBits.Keyword);
  }

  static ElaboratedTypeKeyword getKeywordForTypeSpec(unsigned TypeSpec);

  static TagTypeKind getTagTypeKindForTypeSpec(unsigned TypeSpec);

  static ElaboratedTypeKeyword getKeywordForTagTypeKind(TagTypeKind Tag);

  static TagTypeKind getTagTypeKindForKeyword(ElaboratedTypeKeyword Keyword);

  static bool KeywordIsTagTypeKind(ElaboratedTypeKeyword Keyword);

  static llvm::StringRef getKeywordName(ElaboratedTypeKeyword Keyword);

  static llvm::StringRef getTagTypeKindName(TagTypeKind Kind) {
    return getKeywordName(getKeywordForTagTypeKind(Kind));
  }

  class CannotCastToThisType {};
  static CannotCastToThisType classof(const Type *);
};

class ElaboratedType final
    : public TypeWithKeyword,
      public llvm::FoldingSetNode,
      private llvm::TrailingObjects<ElaboratedType, TagDecl *> {
  friend class TreeContext;
  friend TrailingObjects;

  QualType NamedType;

  ElaboratedType(ElaboratedTypeKeyword Keyword, QualType NamedType,
                 QualType CanonType, TagDecl *OwnedTagDecl)
      : TypeWithKeyword(Keyword, Elaborated, CanonType,
                        NamedType->getDependence()),
        NamedType(NamedType) {
    ElaboratedTypeBits.HasOwnedTagDecl = false;
    if (OwnedTagDecl) {
      ElaboratedTypeBits.HasOwnedTagDecl = true;
      *getTrailingObjects<TagDecl *>() = OwnedTagDecl;
    }
  }

public:
  QualType getNamedType() const { return NamedType; }
  QualType desugar() const { return getNamedType(); }
  bool isSugared() const { return true; }

  TagDecl *getOwnedTagDecl() const {
    return ElaboratedTypeBits.HasOwnedTagDecl ? *getTrailingObjects<TagDecl *>()
                                              : nullptr;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    Profile(ID, getKeyword(), NamedType, getOwnedTagDecl());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, ElaboratedTypeKeyword Keyword,
                      QualType NamedType, TagDecl *OwnedTagDecl) {
    ID.AddInteger(llvm::to_underlying(Keyword));
    NamedType.Profile(ID);
    ID.AddPointer(OwnedTagDecl);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Elaborated; }
};

class AtomicType : public Type, public llvm::FoldingSetNode {
  friend class TreeContext; // TreeContext creates these.

  QualType ValueType;

  AtomicType(QualType ValTy, QualType Canonical)
      : Type(Atomic, Canonical, ValTy->getDependence()), ValueType(ValTy) {}

public:
  QualType getValueType() const { return ValueType; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getValueType()); }

  static void Profile(llvm::FoldingSetNodeID &ID, QualType T) {
    ID.AddPointer(T.getAsOpaquePtr());
  }

  static bool classof(const Type *T) { return T->getTypeClass() == Atomic; }
};

class BitIntType final : public Type, public llvm::FoldingSetNode {
  friend class TreeContext;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsUnsigned : 1;
  unsigned NumBits : 24;

protected:
  BitIntType(bool isUnsigned, unsigned NumBits);

public:
  bool isUnsigned() const { return IsUnsigned; }
  bool isSigned() const { return !IsUnsigned; }
  unsigned getNumBits() const { return NumBits; }

  bool isSugared() const { return false; }
  QualType desugar() const { return QualType(this, 0); }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, isUnsigned(), getNumBits());
  }

  static void Profile(llvm::FoldingSetNodeID &ID, bool IsUnsigned,
                      unsigned NumBits) {
    ID.AddBoolean(IsUnsigned);
    ID.AddInteger(NumBits);
  }

  static bool classof(const Type *T) { return T->getTypeClass() == BitInt; }
};

class QualifierCollector : public Qualifiers {
public:
  QualifierCollector(Qualifiers Qs = Qualifiers()) : Qualifiers(Qs) {}

  const Type *strip(QualType type) {
    addFastQualifiers(type.getLocalFastQualifiers());
    if (!type.hasLocalNonFastQualifiers())
      return type.getTypePtrUnsafe();

    const ExtQuals *extQuals = type.getExtQualsUnsafe();
    addConsistentQualifiers(extQuals->getQualifiers());
    return extQuals->getBaseType();
  }

  QualType apply(const TreeContext &Context, QualType QT) const;

  QualType apply(const TreeContext &Context, const Type *T) const;
};

class alignas(8) TypeSourceInfo {
  // Contains a memory block after the class, used for type source information,
  // allocated by TreeContext.
  friend class TreeContext;

  QualType Ty;

  TypeSourceInfo(QualType ty, size_t DataSize); // implemented in TypeLoc.h

public:
  QualType getType() const { return Ty; }

  TypeLoc getTypeLoc() const; // implemented in TypeLoc.h

  void overrideType(QualType T) { Ty = T; }
};

// Inline function definitions.

inline SplitQualType SplitQualType::getSingleStepDesugaredType() const {
  SplitQualType desugar =
      Ty->getLocallyUnqualifiedSingleStepDesugaredType().split();
  desugar.Quals.addConsistentQualifiers(Quals);
  return desugar;
}

inline const Type *QualType::getTypePtr() const {
  return getCommonPtr()->BaseType;
}

inline const Type *QualType::getTypePtrOrNull() const {
  return (isNull() ? nullptr : getCommonPtr()->BaseType);
}

inline bool QualType::isReferenceable() const {
  const Type &Self = **this;
  return Self.isObjectType() || isa<FunctionType>(Self);
}

inline SplitQualType QualType::split() const {
  if (!hasLocalNonFastQualifiers())
    return SplitQualType(getTypePtrUnsafe(),
                         Qualifiers::fromFastMask(getLocalFastQualifiers()));

  const ExtQuals *eq = getExtQualsUnsafe();
  Qualifiers qs = eq->getQualifiers();
  qs.addFastQualifiers(getLocalFastQualifiers());
  return SplitQualType(eq->getBaseType(), qs);
}

inline Qualifiers QualType::getLocalQualifiers() const {
  Qualifiers Quals;
  if (hasLocalNonFastQualifiers())
    Quals = getExtQualsUnsafe()->getQualifiers();
  Quals.addFastQualifiers(getLocalFastQualifiers());
  return Quals;
}

inline Qualifiers QualType::getQualifiers() const {
  Qualifiers quals = getCommonPtr()->CanonicalType.getLocalQualifiers();
  quals.addFastQualifiers(getLocalFastQualifiers());
  return quals;
}

inline unsigned QualType::getCVRQualifiers() const {
  unsigned cvr = getCommonPtr()->CanonicalType.getLocalCVRQualifiers();
  cvr |= getLocalCVRQualifiers();
  return cvr;
}

inline QualType QualType::getCanonicalType() const {
  QualType canon = getCommonPtr()->CanonicalType;
  return canon.withFastQualifiers(getLocalFastQualifiers());
}

inline bool QualType::isCanonical() const {
  return getTypePtr()->isCanonicalUnqualified();
}

inline bool QualType::isCanonicalAsParam() const {
  if (!isCanonical())
    return false;
  if (hasLocalQualifiers())
    return false;

  const Type *T = getTypePtr();
  if (T->isVariablyModifiedType() && T->hasSizedVLAType())
    return false;

  return !isa<FunctionType>(T) && !isa<ArrayType>(T);
}

inline bool QualType::isConstQualified() const {
  return isLocalConstQualified() ||
         getCommonPtr()->CanonicalType.isLocalConstQualified();
}

inline bool QualType::isRestrictQualified() const {
  return isLocalRestrictQualified() ||
         getCommonPtr()->CanonicalType.isLocalRestrictQualified();
}

inline bool QualType::isVolatileQualified() const {
  return isLocalVolatileQualified() ||
         getCommonPtr()->CanonicalType.isLocalVolatileQualified();
}

inline bool QualType::hasQualifiers() const {
  return hasLocalQualifiers() ||
         getCommonPtr()->CanonicalType.hasLocalQualifiers();
}

inline QualType QualType::getUnqualifiedType() const {
  if (!getTypePtr()->getCanonicalTypeInternal().hasLocalQualifiers())
    return QualType(getTypePtr(), 0);

  return QualType(getSplitUnqualifiedTypeImpl(*this).Ty, 0);
}

inline SplitQualType QualType::getSplitUnqualifiedType() const {
  if (!getTypePtr()->getCanonicalTypeInternal().hasLocalQualifiers())
    return split();

  return getSplitUnqualifiedTypeImpl(*this);
}

inline void QualType::removeLocalConst() {
  removeLocalFastQualifiers(Qualifiers::Const);
}

inline void QualType::removeLocalRestrict() {
  removeLocalFastQualifiers(Qualifiers::Restrict);
}

inline void QualType::removeLocalVolatile() {
  removeLocalFastQualifiers(Qualifiers::Volatile);
}

inline bool QualType::hasAddressSpace() const {
  return getQualifiers().hasAddressSpace();
}

inline LangAS QualType::getAddressSpace() const {
  return getQualifiers().getAddressSpace();
}

inline bool QualType::hasNonTrivialToPrimitiveDefaultInitializeCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveDefaultInitializeCUnion(RD);
  return false;
}

inline bool QualType::hasNonTrivialToPrimitiveDestructCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveDestructCUnion(RD);
  return false;
}

inline bool QualType::hasNonTrivialToPrimitiveCopyCUnion() const {
  if (auto *RD = getTypePtr()->getBaseElementTypeUnsafe()->getAsRecordDecl())
    return hasNonTrivialToPrimitiveCopyCUnion(RD);
  return false;
}

inline FunctionType::ExtInfo getFunctionExtInfo(const Type &t) {
  if (const auto *PT = t.getAs<PointerType>()) {
    if (const auto *FT = PT->getPointeeType()->getAs<FunctionType>())
      return FT->getExtInfo();
  } else if (const auto *FT = t.getAs<FunctionType>())
    return FT->getExtInfo();

  return FunctionType::ExtInfo();
}

inline FunctionType::ExtInfo getFunctionExtInfo(QualType t) {
  return getFunctionExtInfo(*t);
}

inline bool QualType::isMoreQualifiedThan(QualType other) const {
  Qualifiers MyQuals = getQualifiers();
  Qualifiers OtherQuals = other.getQualifiers();
  return (MyQuals != OtherQuals && MyQuals.compatiblyIncludes(OtherQuals));
}

inline bool QualType::isAtLeastAsQualifiedAs(QualType other) const {
  Qualifiers OtherQuals = other.getQualifiers();

  // Ignore __unaligned qualifier if this type is a void.
  if (getUnqualifiedType()->isVoidType())
    OtherQuals.removeUnaligned();

  return getQualifiers().compatiblyIncludes(OtherQuals);
}

inline bool QualType::isCForbiddenLValueType() const {
  return ((getTypePtr()->isVoidType() && !hasQualifiers()) ||
          getTypePtr()->isFunctionType());
}

inline bool Type::isFunctionType() const {
  return isa<FunctionType>(CanonicalType);
}

inline bool Type::isPointerType() const {
  return isa<PointerType>(CanonicalType);
}

inline bool Type::isAnyPointerType() const { return isPointerType(); }

inline bool Type::isObjectPointerType() const {
  // Note: an "object pointer type" is not the same thing as a pointer to an
  // object type; rather, it is a pointer to an object type or a pointer to cv
  // void.
  if (const auto *T = getAs<PointerType>())
    return !T->getPointeeType()->isFunctionType();
  else
    return false;
}

inline bool Type::isFunctionPointerType() const {
  if (const auto *T = getAs<PointerType>())
    return T->getPointeeType()->isFunctionType();
  else
    return false;
}

inline bool Type::isArrayType() const { return isa<ArrayType>(CanonicalType); }

inline bool Type::isConstantArrayType() const {
  return isa<ConstantArrayType>(CanonicalType);
}

inline bool Type::isIncompleteArrayType() const {
  return isa<IncompleteArrayType>(CanonicalType);
}

inline bool Type::isVariableArrayType() const {
  return isa<VariableArrayType>(CanonicalType);
}

inline bool Type::isBuiltinType() const {
  return isa<BuiltinType>(CanonicalType);
}

inline bool Type::isRecordType() const {
  return isa<RecordType>(CanonicalType);
}

inline bool Type::isEnumeralType() const {
  return isa<EnumType>(CanonicalType);
}

inline bool Type::isAnyComplexType() const {
  return isa<ComplexType>(CanonicalType);
}

inline bool Type::isVectorType() const {
  return isa<VectorType>(CanonicalType);
}

inline bool Type::isExtVectorType() const {
  return isa<ExtVectorType>(CanonicalType);
}

inline bool Type::isExtVectorBoolType() const {
  if (!isExtVectorType())
    return false;
  return cast<ExtVectorType>(CanonicalType)->getElementType()->isBooleanType();
}

inline bool Type::isMatrixType() const {
  return isa<MatrixType>(CanonicalType);
}

inline bool Type::isConstantMatrixType() const {
  return isa<ConstantMatrixType>(CanonicalType);
}

inline bool Type::isAtomicType() const {
  return isa<AtomicType>(CanonicalType);
}

inline bool Type::isUndeducedAutoType() const {
  return isa<AutoType>(CanonicalType);
}

inline bool Type::isBitIntType() const {
  return isa<BitIntType>(CanonicalType);
}

inline bool Type::isSpecificBuiltinType(unsigned K) const {
  if (const BuiltinType *BT = getAs<BuiltinType>()) {
    return BT->getKind() == static_cast<BuiltinType::Kind>(K);
  }
  return false;
}

inline bool Type::isPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    return BT->isPlaceholderType();
  return false;
}

inline const BuiltinType *Type::getAsPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    if (BT->isPlaceholderType())
      return BT;
  return nullptr;
}

inline bool Type::isSpecificPlaceholderType(unsigned K) const {
  assert(BuiltinType::isPlaceholderTypeKind((BuiltinType::Kind)K));
  return isSpecificBuiltinType(K);
}

inline bool Type::isNonOverloadPlaceholderType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(this))
    return BT->isNonOverloadPlaceholderType();
  return false;
}

inline bool Type::isVoidType() const {
  return isSpecificBuiltinType(BuiltinType::Void);
}

inline bool Type::isHalfType() const {
  return isSpecificBuiltinType(BuiltinType::Half);
}

inline bool Type::isFloat16Type() const {
  return isSpecificBuiltinType(BuiltinType::Float16);
}

inline bool Type::isBFloat16Type() const {
  return isSpecificBuiltinType(BuiltinType::BFloat16);
}

inline bool Type::isFloat128Type() const {
  return isSpecificBuiltinType(BuiltinType::Float128);
}

inline bool Type::isNullPtrType() const {
  return isSpecificBuiltinType(BuiltinType::NullPtr);
}

bool IsEnumDeclComplete(EnumDecl *);

inline bool Type::isIntegerType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;
  if (const EnumType *ET = dyn_cast<EnumType>(CanonicalType))
    return IsEnumDeclComplete(ET->getDecl());
  return isBitIntType();
}

inline bool Type::isFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::ShortAccum &&
           BT->getKind() <= BuiltinType::SatULongFract;
  }
  return false;
}

inline bool Type::isFixedPointOrIntegerType() const {
  return isFixedPointType() || isIntegerType();
}

inline bool Type::isSaturatedFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return BT->getKind() >= BuiltinType::SatShortAccum &&
           BT->getKind() <= BuiltinType::SatULongFract;
  }
  return false;
}

inline bool Type::isUnsaturatedFixedPointType() const {
  return isFixedPointType() && !isSaturatedFixedPointType();
}

inline bool Type::isSignedFixedPointType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType)) {
    return ((BT->getKind() >= BuiltinType::ShortAccum &&
             BT->getKind() <= BuiltinType::LongAccum) ||
            (BT->getKind() >= BuiltinType::ShortFract &&
             BT->getKind() <= BuiltinType::LongFract) ||
            (BT->getKind() >= BuiltinType::SatShortAccum &&
             BT->getKind() <= BuiltinType::SatLongAccum) ||
            (BT->getKind() >= BuiltinType::SatShortFract &&
             BT->getKind() <= BuiltinType::SatLongFract));
  }
  return false;
}

inline bool Type::isUnsignedFixedPointType() const {
  return isFixedPointType() && !isSignedFixedPointType();
}

inline bool Type::isScalarType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() > BuiltinType::Void &&
           BT->getKind() <= BuiltinType::NullPtr;
  if (const EnumType *ET = dyn_cast<EnumType>(CanonicalType))
    // Enums are scalar types, but only if they are defined.  Incomplete enums
    // are not treated as scalar types.
    return IsEnumDeclComplete(ET->getDecl());
  return isa<PointerType>(CanonicalType) || isa<ComplexType>(CanonicalType) ||
         isBitIntType();
}

inline bool Type::isIntegralOrEnumerationType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() >= BuiltinType::Bool &&
           BT->getKind() <= BuiltinType::Int128;

  // Check for a complete enum type; incomplete enum types are not properly an
  // enumeration type in the sense required here.
  if (const auto *ET = dyn_cast<EnumType>(CanonicalType))
    return IsEnumDeclComplete(ET->getDecl());

  return isBitIntType();
}

inline bool Type::isBooleanType() const {
  if (const auto *BT = dyn_cast<BuiltinType>(CanonicalType))
    return BT->getKind() == BuiltinType::Bool;
  return false;
}

inline bool Type::isUndeducedType() const {
  auto *DT = getContainedDeducedType();
  return DT && !DT->isDeduced();
}

inline bool Type::isTypedefNameType() const {
  return getAs<TypedefType>() != nullptr;
}

inline bool Type::canDecayToPointerType() const {
  return isFunctionType() || isArrayType();
}

inline bool Type::hasPointerRepresentation() const {
  return (isPointerType() || isNullPtrType());
}

inline const Type *Type::getBaseElementTypeUnsafe() const {
  const Type *type = this;
  while (const ArrayType *arrayType = type->getAsArrayTypeUnsafe())
    type = arrayType->getElementType().getTypePtr();
  return type;
}

inline const Type *Type::getPointeeOrArrayElementType() const {
  const Type *type = this;
  if (type->isAnyPointerType())
    return type->getPointeeType().getTypePtr();
  else if (type->isArrayType())
    return type->getBaseElementTypeUnsafe();
  return type;
}
inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             LangAS AS) {
  PD.AddTaggedVal(llvm::to_underlying(AS),
                  DiagnosticsEngine::ArgumentKind::ak_addrspace);
  return PD;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             Qualifiers Q) {
  PD.AddTaggedVal(Q.getAsOpaqueValue(),
                  DiagnosticsEngine::ArgumentKind::ak_qual);
  return PD;
}

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &PD,
                                             QualType T) {
  PD.AddTaggedVal(reinterpret_cast<uint64_t>(T.getAsOpaquePtr()),
                  DiagnosticsEngine::ak_qualtype);
  return PD;
}

// Helper class template that is used by Type::getAs to ensure that one does
// not try to look through a qualified type to get to an array type.
template <typename T>
using TypeIsArrayType =
    std::integral_constant<bool, std::is_same<T, ArrayType>::value ||
                                     std::is_base_of<ArrayType, T>::value>;

// Member-template getAs<specific type>'.
template <typename T> const T *Type::getAs() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with getAs!");

  // If this is directly a T type, return it.
  if (const auto *Ty = dyn_cast<T>(this))
    return Ty;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<T>(CanonicalType))
    return nullptr;

  // If this is a typedef for the type, strip the typedef off without
  // losing all typedef information.
  return cast<T>(getUnqualifiedDesugaredType());
}

template <typename T> const T *Type::getAsAdjusted() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with getAsAdjusted!");

  // If this is directly a T type, return it.
  if (const auto *Ty = dyn_cast<T>(this))
    return Ty;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<T>(CanonicalType))
    return nullptr;

  // Strip off type adjustments that do not modify the underlying nature of the
  // type.
  const Type *Ty = this;
  while (Ty) {
    if (const auto *A = dyn_cast<AttributedType>(Ty))
      Ty = A->getModifiedType().getTypePtr();
    else if (const auto *A = dyn_cast<BTFTagAttributedType>(Ty))
      Ty = A->getWrappedType().getTypePtr();
    else if (const auto *E = dyn_cast<ElaboratedType>(Ty))
      Ty = E->desugar().getTypePtr();
    else if (const auto *P = dyn_cast<ParenType>(Ty))
      Ty = P->desugar().getTypePtr();
    else if (const auto *A = dyn_cast<AdjustedType>(Ty))
      Ty = A->desugar().getTypePtr();
    else if (const auto *M = dyn_cast<MacroQualifiedType>(Ty))
      Ty = M->desugar().getTypePtr();
    else
      break;
  }

  // Just because the canonical type is correct does not mean we can use cast<>,
  // since we may not have stripped off all the sugar down to the base type.
  return dyn_cast<T>(Ty);
}

inline const ArrayType *Type::getAsArrayTypeUnsafe() const {
  // If this is directly an array type, return it.
  if (const auto *arr = dyn_cast<ArrayType>(this))
    return arr;

  // If the canonical form of this type isn't the right kind, reject it.
  if (!isa<ArrayType>(CanonicalType))
    return nullptr;

  // If this is a typedef for the type, strip the typedef off without
  // losing all typedef information.
  return cast<ArrayType>(getUnqualifiedDesugaredType());
}

template <typename T> const T *Type::castAs() const {
  static_assert(!TypeIsArrayType<T>::value,
                "ArrayType cannot be used with castAs!");

  if (const auto *ty = dyn_cast<T>(this))
    return ty;
  assert(isa<T>(CanonicalType));
  return cast<T>(getUnqualifiedDesugaredType());
}

inline const ArrayType *Type::castAsArrayTypeUnsafe() const {
  assert(isa<ArrayType>(CanonicalType));
  if (const auto *arr = dyn_cast<ArrayType>(this))
    return arr;
  return cast<ArrayType>(getUnqualifiedDesugaredType());
}

DecayedType::DecayedType(QualType OriginalType, QualType DecayedPtr,
                         QualType CanonicalPtr)
    : AdjustedType(Decayed, OriginalType, DecayedPtr, CanonicalPtr) {
#ifndef NDEBUG
  QualType Adjusted = getAdjustedType();
  (void)AttributedType::stripOuterNullability(Adjusted);
  assert(isa<PointerType>(Adjusted));
#endif
}

QualType DecayedType::getPointeeType() const {
  QualType Decayed = getDecayedType();
  (void)AttributedType::stripOuterNullability(Decayed);
  return cast<PointerType>(Decayed)->getPointeeType();
}

// Get the decimal string representation of a fixed point type, represented
// as a scaled integer.
void FixedPointValueToString(llvm::SmallVectorImpl<char> &Str, llvm::APSInt Val,
                             unsigned Scale);

} // namespace neverc

#endif // NEVERC_AST_TYPE_H
