#ifndef NEVERC_TREE_ATTR_H
#define NEVERC_TREE_ATTR_H

#include "neverc/Foundation/Attr/AttrKinds.h"
#include "neverc/Foundation/Attr/AttributeCommonInfo.h"
#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/LangOpts/LangOptions.h"
#include "neverc/Tree/Core/AttrIterator.h"
#include "neverc/Tree/Core/TreeFwd.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

namespace neverc {

using llvm::dyn_cast;
using llvm::raw_ostream;
using llvm::SmallVector;
using llvm::StringRef;
using llvm::VersionTuple;

class TreeContext;
class AttributeCommonInfo;

class Attr : public AttributeCommonInfo {
private:
  LLVM_PREFERRED_TYPE(attr::Kind)
  unsigned AttrKind : 16;

protected:
  LLVM_PREFERRED_TYPE(bool)
  unsigned Inherited : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned Implicit : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsLateParsed : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned InheritEvenIfAlreadyPresent : 1;

  void *operator new(size_t bytes) noexcept {
    llvm_unreachable("Attrs cannot be allocated with regular 'new'.");
  }
  void operator delete(void *data) noexcept {
    llvm_unreachable("Attrs cannot be released with regular 'delete'.");
  }

public:
  // Forward so that the regular new and delete do not hide global ones.
  void *operator new(size_t Bytes, TreeContext &C,
                     size_t Alignment = 8) noexcept {
    return ::operator new(Bytes, C, Alignment);
  }
  void operator delete(void *Ptr, TreeContext &C, size_t Alignment) noexcept {
    return ::operator delete(Ptr, C, Alignment);
  }

protected:
  Attr(TreeContext &Context, const AttributeCommonInfo &CommonInfo,
       attr::Kind AK, bool IsLateParsed)
      : AttributeCommonInfo(CommonInfo), AttrKind(AK), Inherited(false),
        Implicit(false), IsLateParsed(IsLateParsed),
        InheritEvenIfAlreadyPresent(false) {}

public:
  attr::Kind getKind() const { return static_cast<attr::Kind>(AttrKind); }

  unsigned getSpellingListIndex() const {
    return getAttributeSpellingListIndex();
  }
  const char *getSpelling() const;

  SourceLocation getLocation() const { return getRange().getBegin(); }

  bool isInherited() const { return Inherited; }

  bool isImplicit() const { return Implicit; }
  void setImplicit(bool I) { Implicit = I; }

  // Clone this attribute.
  Attr *clone(TreeContext &C) const;

  bool isLateParsed() const { return IsLateParsed; }

  // Pretty print this attribute.
  void printPretty(llvm::raw_ostream &OS, const PrintingPolicy &Policy) const;

  static llvm::StringRef getDocumentation(attr::Kind) { return {}; }
};

class TypeAttr : public Attr {
protected:
  TypeAttr(TreeContext &Context, const AttributeCommonInfo &CommonInfo,
           attr::Kind AK, bool IsLateParsed)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstTypeAttr &&
           A->getKind() <= attr::LastTypeAttr;
  }
};

class StmtAttr : public Attr {
protected:
  StmtAttr(TreeContext &Context, const AttributeCommonInfo &CommonInfo,
           attr::Kind AK, bool IsLateParsed)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstStmtAttr &&
           A->getKind() <= attr::LastStmtAttr;
  }
};

class InheritableAttr : public Attr {
protected:
  InheritableAttr(TreeContext &Context, const AttributeCommonInfo &CommonInfo,
                  attr::Kind AK, bool IsLateParsed,
                  bool InheritEvenIfAlreadyPresent)
      : Attr(Context, CommonInfo, AK, IsLateParsed) {
    this->InheritEvenIfAlreadyPresent = InheritEvenIfAlreadyPresent;
  }

public:
  void setInherited(bool I) { Inherited = I; }

  bool shouldInheritEvenIfAlreadyPresent() const {
    return InheritEvenIfAlreadyPresent;
  }

  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableAttr &&
           A->getKind() <= attr::LastInheritableAttr;
  }
};

class DeclOrStmtAttr : public InheritableAttr {
protected:
  DeclOrStmtAttr(TreeContext &Context, const AttributeCommonInfo &CommonInfo,
                 attr::Kind AK, bool IsLateParsed,
                 bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(Context, CommonInfo, AK, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstDeclOrStmtAttr &&
           A->getKind() <= attr::LastDeclOrStmtAttr;
  }
};

class InheritableParamAttr : public InheritableAttr {
protected:
  InheritableParamAttr(TreeContext &Context,
                       const AttributeCommonInfo &CommonInfo, attr::Kind AK,
                       bool IsLateParsed, bool InheritEvenIfAlreadyPresent)
      : InheritableAttr(Context, CommonInfo, AK, IsLateParsed,
                        InheritEvenIfAlreadyPresent) {}

public:
  // Implement isa/cast/dyncast/etc.
  static bool classof(const Attr *A) {
    return A->getKind() >= attr::FirstInheritableParamAttr &&
           A->getKind() <= attr::LastInheritableParamAttr;
  }
};

class ParamIdx {
  // Idx is exposed only via accessors that specify specific encodings.
  unsigned Idx : 30;
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasThis : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsValid : 1;

  void assertComparable(const ParamIdx &I) const {
    assert(isValid() && I.isValid() && "ParamIdx must be valid to be compared");
    // It's possible to compare indices from separate functions, but so far
    // it's not proven useful.  Moreover, it might be confusing because a
    // comparison on the results of getASTIndex might be inconsistent with a
    // comparison on the ParamIdx objects themselves.
    assert(HasThis == I.HasThis &&
           "ParamIdx must be for the same function to be compared");
  }

public:
  ParamIdx() : Idx(0), HasThis(false), IsValid(false) {}

  ParamIdx(unsigned Idx, const Decl *D)
      : Idx(Idx), HasThis(false), IsValid(true) {
    assert(Idx >= 1 && "Idx must be one-origin");
    (void)D;
  }

  typedef uint32_t SerialType;

  SerialType serialize() const {
    return *reinterpret_cast<const SerialType *>(this);
  }

  static ParamIdx deserialize(SerialType S) {
    // Using this two-step static_cast via void * instead of reinterpret_cast
    // silences a -Wstrict-aliasing false positive from GCC7 and earlier.
    void *ParamIdxPtr = static_cast<void *>(&S);
    ParamIdx P(*static_cast<ParamIdx *>(ParamIdxPtr));
    assert((!P.IsValid || P.Idx >= 1) && "valid Idx must be one-origin");
    return P;
  }

  bool isValid() const { return IsValid; }

  unsigned getSourceIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    return Idx;
  }

  unsigned getASTIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 + HasThis && "stored index must be base-1");
    return Idx - 1 - HasThis;
  }

  unsigned getLLVMIndex() const {
    assert(isValid() && "ParamIdx must be valid");
    assert(Idx >= 1 && "stored index must be base-1");
    return Idx - 1;
  }

  bool operator==(const ParamIdx &I) const {
    assertComparable(I);
    return Idx == I.Idx;
  }
  bool operator!=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx != I.Idx;
  }
  bool operator<(const ParamIdx &I) const {
    assertComparable(I);
    return Idx < I.Idx;
  }
  bool operator>(const ParamIdx &I) const {
    assertComparable(I);
    return Idx > I.Idx;
  }
  bool operator<=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx <= I.Idx;
  }
  bool operator>=(const ParamIdx &I) const {
    assertComparable(I);
    return Idx >= I.Idx;
  }
};

static_assert(sizeof(ParamIdx) == sizeof(ParamIdx::SerialType),
              "ParamIdx does not fit its serialization type");

#include "neverc/Tree/Attrs.td.h"

inline const StreamingDiagnostic &operator<<(const StreamingDiagnostic &DB,
                                             const Attr *At) {
  DB.AddTaggedVal(reinterpret_cast<uint64_t>(At), DiagnosticsEngine::ak_attr);
  return DB;
}
} // end namespace neverc

#endif
