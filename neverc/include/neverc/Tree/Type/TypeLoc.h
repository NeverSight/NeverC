#ifndef NEVERC_AST_TYPELOC_H
#define NEVERC_AST_TYPELOC_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/Specifiers.h"
#include "neverc/Tree/Decl/DeclarationName.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace neverc {

using llvm::cast;
using llvm::dyn_cast;
using llvm::isa;

class Attr;
class TreeContext;
class Expr;
class ParmVarDecl;
class UnqualTypeLoc;

// Predeclare all the type nodes.
#define ABSTRACT_TYPELOC(Class, Base)
#define TYPELOC(Class, Base) class Class##TypeLoc;
#include "neverc/Tree/Type/TypeLocNodes.def"

class TypeLoc {
protected:
  // The correctness of this relies on the property that, for Type *Ty,
  //   QualType(Ty, 0).getAsOpaquePtr() == (void*) Ty
  const void *Ty = nullptr;
  void *Data = nullptr;

public:
  TypeLoc() = default;
  TypeLoc(QualType ty, void *opaqueData)
      : Ty(ty.getAsOpaquePtr()), Data(opaqueData) {}
  TypeLoc(const Type *ty, void *opaqueData) : Ty(ty), Data(opaqueData) {}

  template <typename T> T castAs() const {
    assert(T::isKind(*this));
    T t;
    TypeLoc &tl = t;
    tl = *this;
    return t;
  }

  template <typename T> T getAs() const {
    if (!T::isKind(*this))
      return {};
    T t;
    TypeLoc &tl = t;
    tl = *this;
    return t;
  }

  template <typename T> T getAsAdjusted() const;

  enum TypeLocClass {
#define ABSTRACT_TYPE(Class, Base)
#define TYPE(Class, Base) Class = Type::Class,
#include "neverc/Tree/TypeNodes.td.h"
    Qualified
  };

  TypeLocClass getTypeLocClass() const {
    if (getType().hasLocalQualifiers())
      return Qualified;
    return (TypeLocClass)getType()->getTypeClass();
  }

  bool isNull() const { return !Ty; }
  explicit operator bool() const { return Ty; }

  static unsigned getFullDataSizeForType(QualType Ty);

  static unsigned getLocalAlignmentForType(QualType Ty);

  QualType getType() const { return QualType::getFromOpaquePtr(Ty); }

  const Type *getTypePtr() const {
    return QualType::getFromOpaquePtr(Ty).getTypePtr();
  }

  void *getOpaqueData() const { return Data; }

  SourceLocation getBeginLoc() const;

  SourceLocation getEndLoc() const;

  SourceRange getSourceRange() const LLVM_READONLY {
    return SourceRange(getBeginLoc(), getEndLoc());
  }

  SourceRange getLocalSourceRange() const {
    return getLocalSourceRangeImpl(*this);
  }

  unsigned getFullDataSize() const { return getFullDataSizeForType(getType()); }

  TypeLoc getNextTypeLoc() const { return getNextTypeLocImpl(*this); }

  UnqualTypeLoc getUnqualifiedLoc() const; // implemented in this header

  TypeLoc IgnoreParens() const;

  TypeLoc findExplicitQualifierLoc() const;

  AutoTypeLoc getContainedAutoTypeLoc() const;

  void initialize(TreeContext &Context, SourceLocation Loc) const {
    initializeImpl(Context, *this, Loc);
  }

  void initializeFullCopy(TypeLoc Other) {
    assert(getType() == Other.getType());
    copy(Other);
  }

  void initializeFullCopy(TypeLoc Other, unsigned Size) {
    assert(getType() == Other.getType());
    assert(getFullDataSize() == Size);
    copy(Other);
  }

  void copy(TypeLoc other);

  friend bool operator==(const TypeLoc &LHS, const TypeLoc &RHS) {
    return LHS.Ty == RHS.Ty && LHS.Data == RHS.Data;
  }

  friend bool operator!=(const TypeLoc &LHS, const TypeLoc &RHS) {
    return !(LHS == RHS);
  }

  SourceLocation findNullabilityLoc() const;

private:
  static bool isKind(const TypeLoc &) { return true; }

  static void initializeImpl(TreeContext &Context, TypeLoc TL,
                             SourceLocation Loc);
  static TypeLoc getNextTypeLocImpl(TypeLoc TL);
  static TypeLoc IgnoreParensImpl(TypeLoc TL);
  static SourceRange getLocalSourceRangeImpl(TypeLoc TL);
};

inline TypeSourceInfo::TypeSourceInfo(QualType ty, size_t DataSize) : Ty(ty) {
  // Init data attached to the object. See getTypeLoc.
  memset(static_cast<void *>(this + 1), 0, DataSize);
}

inline TypeLoc TypeSourceInfo::getTypeLoc() const {
  return TypeLoc(Ty, const_cast<void *>(static_cast<const void *>(this + 1)));
}

class UnqualTypeLoc : public TypeLoc {
public:
  UnqualTypeLoc() = default;
  UnqualTypeLoc(const Type *Ty, void *Data) : TypeLoc(Ty, Data) {}

  const Type *getTypePtr() const { return reinterpret_cast<const Type *>(Ty); }

  TypeLocClass getTypeLocClass() const {
    return (TypeLocClass)getTypePtr()->getTypeClass();
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers();
  }
};

class QualifiedTypeLoc : public TypeLoc {
public:
  SourceRange getLocalSourceRange() const { return {}; }

  UnqualTypeLoc getUnqualifiedLoc() const {
    unsigned align =
        TypeLoc::getLocalAlignmentForType(QualType(getTypePtr(), 0));
    auto dataInt = reinterpret_cast<uintptr_t>(Data);
    dataInt = llvm::alignTo(dataInt, align);
    return UnqualTypeLoc(getTypePtr(), reinterpret_cast<void *>(dataInt));
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    // do nothing
  }

  void copyLocal(TypeLoc other) {
    // do nothing
  }

  TypeLoc getNextTypeLoc() const { return getUnqualifiedLoc(); }

  unsigned getLocalDataSize() const {
    // In fact, we don't currently preserve any location information
    // for qualifiers.
    return 0;
  }

  unsigned getLocalDataAlignment() const {
    // We don't preserve any location information.
    return 1;
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL) {
    return TL.getType().hasLocalQualifiers();
  }
};

inline UnqualTypeLoc TypeLoc::getUnqualifiedLoc() const {
  if (QualifiedTypeLoc Loc = getAs<QualifiedTypeLoc>())
    return Loc.getUnqualifiedLoc();
  return castAs<UnqualTypeLoc>();
}

template <class Base, class Derived, class TypeClass, class LocalData>
class ConcreteTypeLoc : public Base {
  friend class TypeLoc;

  const Derived *asDerived() const {
    return static_cast<const Derived *>(this);
  }

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers() &&
           Derived::classofType(TL.getTypePtr());
  }

  static bool classofType(const Type *Ty) { return TypeClass::classof(Ty); }

public:
  unsigned getLocalDataAlignment() const {
    return std::max(unsigned(alignof(LocalData)),
                    asDerived()->getExtraLocalDataAlignment());
  }

  unsigned getLocalDataSize() const {
    unsigned size = sizeof(LocalData);
    unsigned extraAlign = asDerived()->getExtraLocalDataAlignment();
    size = llvm::alignTo(size, extraAlign);
    size += asDerived()->getExtraLocalDataSize();
    return size;
  }

  void copyLocal(Derived other) {
    // Some subclasses have no data to copy.
    if (asDerived()->getLocalDataSize() == 0)
      return;

    // Copy the fixed-sized local data.
    memcpy(getLocalData(), other.getLocalData(), sizeof(LocalData));

    // Copy the variable-sized local data. We need to do this
    // separately because the padding in the source and the padding in
    // the destination might be different.
    memcpy(getExtraLocalData(), other.getExtraLocalData(),
           asDerived()->getExtraLocalDataSize());
  }

  TypeLoc getNextTypeLoc() const {
    return getNextTypeLoc(asDerived()->getInnerType());
  }

  const TypeClass *getTypePtr() const {
    return cast<TypeClass>(Base::getTypePtr());
  }

protected:
  unsigned getExtraLocalDataSize() const { return 0; }

  unsigned getExtraLocalDataAlignment() const { return 1; }

  LocalData *getLocalData() const {
    return static_cast<LocalData *>(Base::Data);
  }

  void *getExtraLocalData() const {
    unsigned size = sizeof(LocalData);
    unsigned extraAlign = asDerived()->getExtraLocalDataAlignment();
    size = llvm::alignTo(size, extraAlign);
    return reinterpret_cast<char *>(Base::Data) + size;
  }

  void *getNonLocalData() const {
    auto data = reinterpret_cast<uintptr_t>(Base::Data);
    data += asDerived()->getLocalDataSize();
    data = llvm::alignTo(data, getNextTypeAlign());
    return reinterpret_cast<void *>(data);
  }

  struct HasNoInnerType {};
  HasNoInnerType getInnerType() const { return HasNoInnerType(); }

  TypeLoc getInnerTypeLoc() const {
    return TypeLoc(asDerived()->getInnerType(), getNonLocalData());
  }

private:
  unsigned getInnerTypeSize() const {
    return getInnerTypeSize(asDerived()->getInnerType());
  }

  unsigned getInnerTypeSize(HasNoInnerType _) const { return 0; }

  unsigned getInnerTypeSize(QualType _) const {
    return getInnerTypeLoc().getFullDataSize();
  }

  unsigned getNextTypeAlign() const {
    return getNextTypeAlign(asDerived()->getInnerType());
  }

  unsigned getNextTypeAlign(HasNoInnerType _) const { return 1; }

  unsigned getNextTypeAlign(QualType T) const {
    return TypeLoc::getLocalAlignmentForType(T);
  }

  TypeLoc getNextTypeLoc(HasNoInnerType _) const { return {}; }

  TypeLoc getNextTypeLoc(QualType T) const {
    return TypeLoc(T, getNonLocalData());
  }
};

template <class Base, class Derived, class TypeClass>
class InheritingConcreteTypeLoc : public Base {
  friend class TypeLoc;

  static bool classofType(const Type *Ty) { return TypeClass::classof(Ty); }

  static bool isKind(const TypeLoc &TL) {
    return !TL.getType().hasLocalQualifiers() &&
           Derived::classofType(TL.getTypePtr());
  }
  static bool isKind(const UnqualTypeLoc &TL) {
    return Derived::classofType(TL.getTypePtr());
  }

public:
  const TypeClass *getTypePtr() const {
    return cast<TypeClass>(Base::getTypePtr());
  }
};

struct TypeSpecLocInfo {
  SourceLocation NameLoc;
};

class TypeSpecTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, TypeSpecTypeLoc,
                                               Type, TypeSpecLocInfo> {
public:
  enum {
    LocalDataSize = sizeof(TypeSpecLocInfo),
    LocalDataAlignment = alignof(TypeSpecLocInfo)
  };

  SourceLocation getNameLoc() const { return this->getLocalData()->NameLoc; }

  void setNameLoc(SourceLocation Loc) { this->getLocalData()->NameLoc = Loc; }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getNameLoc(), getNameLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setNameLoc(Loc);
  }

private:
  friend class TypeLoc;

  static bool isKind(const TypeLoc &TL);
};

struct BuiltinLocInfo {
  SourceRange BuiltinRange;
};

class BuiltinTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, BuiltinTypeLoc,
                                              BuiltinType, BuiltinLocInfo> {
public:
  SourceLocation getBuiltinLoc() const {
    return getLocalData()->BuiltinRange.getBegin();
  }

  void setBuiltinLoc(SourceLocation Loc) { getLocalData()->BuiltinRange = Loc; }

  void expandBuiltinRange(SourceRange Range) {
    SourceRange &BuiltinRange = getLocalData()->BuiltinRange;
    if (!BuiltinRange.getBegin().isValid()) {
      BuiltinRange = Range;
    } else {
      BuiltinRange.setBegin(
          std::min(Range.getBegin(), BuiltinRange.getBegin()));
      BuiltinRange.setEnd(std::max(Range.getEnd(), BuiltinRange.getEnd()));
    }
  }

  SourceLocation getNameLoc() const { return getBuiltinLoc(); }

  WrittenBuiltinSpecs &getWrittenBuiltinSpecs() {
    return *(static_cast<WrittenBuiltinSpecs *>(getExtraLocalData()));
  }
  const WrittenBuiltinSpecs &getWrittenBuiltinSpecs() const {
    return *(static_cast<WrittenBuiltinSpecs *>(getExtraLocalData()));
  }

  bool needsExtraLocalData() const {
    BuiltinType::Kind bk = getTypePtr()->getKind();
    return (bk >= BuiltinType::UShort && bk <= BuiltinType::UInt128) ||
           (bk >= BuiltinType::Short && bk <= BuiltinType::Float128) ||
           bk == BuiltinType::UChar || bk == BuiltinType::SChar;
  }

  unsigned getExtraLocalDataSize() const {
    return needsExtraLocalData() ? sizeof(WrittenBuiltinSpecs) : 0;
  }

  unsigned getExtraLocalDataAlignment() const {
    return needsExtraLocalData() ? alignof(WrittenBuiltinSpecs) : 1;
  }

  SourceRange getLocalSourceRange() const {
    return getLocalData()->BuiltinRange;
  }

  TypeSpecifierSign getWrittenSignSpec() const {
    if (needsExtraLocalData())
      return static_cast<TypeSpecifierSign>(getWrittenBuiltinSpecs().Sign);
    else
      return TypeSpecifierSign::Unspecified;
  }

  bool hasWrittenSignSpec() const {
    return getWrittenSignSpec() != TypeSpecifierSign::Unspecified;
  }

  void setWrittenSignSpec(TypeSpecifierSign written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Sign = static_cast<unsigned>(written);
  }

  TypeSpecifierWidth getWrittenWidthSpec() const {
    if (needsExtraLocalData())
      return static_cast<TypeSpecifierWidth>(getWrittenBuiltinSpecs().Width);
    else
      return TypeSpecifierWidth::Unspecified;
  }

  bool hasWrittenWidthSpec() const {
    return getWrittenWidthSpec() != TypeSpecifierWidth::Unspecified;
  }

  void setWrittenWidthSpec(TypeSpecifierWidth written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Width = static_cast<unsigned>(written);
  }

  TypeSpecifierType getWrittenTypeSpec() const;

  bool hasWrittenTypeSpec() const {
    return getWrittenTypeSpec() != TST_unspecified;
  }

  void setWrittenTypeSpec(TypeSpecifierType written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().Type = written;
  }

  bool hasModeAttr() const {
    if (needsExtraLocalData())
      return getWrittenBuiltinSpecs().ModeAttr;
    else
      return false;
  }

  void setModeAttr(bool written) {
    if (needsExtraLocalData())
      getWrittenBuiltinSpecs().ModeAttr = written;
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setBuiltinLoc(Loc);
    if (needsExtraLocalData()) {
      WrittenBuiltinSpecs &wbs = getWrittenBuiltinSpecs();
      wbs.Sign = static_cast<unsigned>(TypeSpecifierSign::Unspecified);
      wbs.Width = static_cast<unsigned>(TypeSpecifierWidth::Unspecified);
      wbs.Type = TST_unspecified;
      wbs.ModeAttr = false;
    }
  }
};

class TypedefTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, TypedefTypeLoc,
                                       TypedefType> {
public:
  TypedefNameDecl *getTypedefNameDecl() const {
    return getTypePtr()->getDecl();
  }
};

class TagTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, TagTypeLoc, TagType> {
public:
  TagDecl *getDecl() const { return getTypePtr()->getDecl(); }

  bool isDefinition() const;
};

class RecordTypeLoc
    : public InheritingConcreteTypeLoc<TagTypeLoc, RecordTypeLoc, RecordType> {
public:
  RecordDecl *getDecl() const { return getTypePtr()->getDecl(); }
};

class EnumTypeLoc
    : public InheritingConcreteTypeLoc<TagTypeLoc, EnumTypeLoc, EnumType> {
public:
  EnumDecl *getDecl() const { return getTypePtr()->getDecl(); }
};

struct AttributedLocInfo {
  const Attr *TypeAttr;
};

class AttributedTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, AttributedTypeLoc, AttributedType,
                             AttributedLocInfo> {
public:
  attr::Kind getAttrKind() const { return getTypePtr()->getAttrKind(); }

  bool isQualifier() const { return getTypePtr()->isQualifier(); }

  TypeLoc getModifiedLoc() const { return getInnerTypeLoc(); }

  const Attr *getAttr() const { return getLocalData()->TypeAttr; }
  void setAttr(const Attr *A) { getLocalData()->TypeAttr = A; }

  template <typename T> const T *getAttrAs() {
    return dyn_cast_or_null<T>(getAttr());
  }

  SourceRange getLocalSourceRange() const;

  void initializeLocal(TreeContext &Context, SourceLocation loc) {
    setAttr(nullptr);
  }

  QualType getInnerType() const { return getTypePtr()->getModifiedType(); }
};

struct BTFTagAttributedLocInfo {}; // Nothing.

class BTFTagAttributedTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, BTFTagAttributedTypeLoc,
                             BTFTagAttributedType, BTFTagAttributedLocInfo> {
public:
  TypeLoc getWrappedLoc() const { return getInnerTypeLoc(); }

  const BTFTypeTagAttr *getAttr() const { return getTypePtr()->getAttr(); }

  template <typename T> T *getAttrAs() {
    return dyn_cast_or_null<T>(getAttr());
  }

  SourceRange getLocalSourceRange() const;

  void initializeLocal(TreeContext &Context, SourceLocation loc) {}

  QualType getInnerType() const { return getTypePtr()->getWrappedType(); }
};

struct MacroQualifiedLocInfo {
  SourceLocation ExpansionLoc;
};

class MacroQualifiedTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, MacroQualifiedTypeLoc,
                             MacroQualifiedType, MacroQualifiedLocInfo> {
public:
  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setExpansionLoc(Loc);
  }

  TypeLoc getInnerLoc() const { return getInnerTypeLoc(); }

  const IdentifierInfo *getMacroIdentifier() const {
    return getTypePtr()->getMacroIdentifier();
  }

  SourceLocation getExpansionLoc() const {
    return this->getLocalData()->ExpansionLoc;
  }

  void setExpansionLoc(SourceLocation Loc) {
    this->getLocalData()->ExpansionLoc = Loc;
  }

  QualType getInnerType() const { return getTypePtr()->getUnderlyingType(); }

  SourceRange getLocalSourceRange() const {
    return getInnerLoc().getLocalSourceRange();
  }
};

struct ParenLocInfo {
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
};

class ParenTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, ParenTypeLoc,
                                            ParenType, ParenLocInfo> {
public:
  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }

  TypeLoc getInnerLoc() const { return getInnerTypeLoc(); }

  QualType getInnerType() const { return this->getTypePtr()->getInnerType(); }
};

inline TypeLoc TypeLoc::IgnoreParens() const {
  if (ParenTypeLoc::isKind(*this))
    return IgnoreParensImpl(*this);
  return *this;
}

struct AdjustedLocInfo {}; // Nothing.

class AdjustedTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, AdjustedTypeLoc,
                                               AdjustedType, AdjustedLocInfo> {
public:
  TypeLoc getOriginalLoc() const { return getInnerTypeLoc(); }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    // do nothing
  }

  QualType getInnerType() const {
    // The inner type is the undecayed type, since that's what we have source
    // location information for.
    return getTypePtr()->getOriginalType();
  }

  SourceRange getLocalSourceRange() const { return {}; }

  unsigned getLocalDataSize() const {
    // sizeof(AdjustedLocInfo) is 1, but we don't need its address to be unique
    // anyway.  TypeLocBuilder can't handle data sizes of 1.
    return 0; // No data.
  }
};

class DecayedTypeLoc
    : public InheritingConcreteTypeLoc<AdjustedTypeLoc, DecayedTypeLoc,
                                       DecayedType> {};

struct PointerLikeLocInfo {
  SourceLocation StarLoc;
};

template <class Derived, class TypeClass, class LocalData = PointerLikeLocInfo>
class PointerLikeTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, Derived, TypeClass, LocalData> {
public:
  SourceLocation getSigilLoc() const { return this->getLocalData()->StarLoc; }

  void setSigilLoc(SourceLocation Loc) { this->getLocalData()->StarLoc = Loc; }

  TypeLoc getPointeeLoc() const { return this->getInnerTypeLoc(); }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getSigilLoc(), getSigilLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setSigilLoc(Loc);
  }

  QualType getInnerType() const { return this->getTypePtr()->getPointeeType(); }
};

class PointerTypeLoc : public PointerLikeTypeLoc<PointerTypeLoc, PointerType> {
public:
  SourceLocation getStarLoc() const { return getSigilLoc(); }

  void setStarLoc(SourceLocation Loc) { setSigilLoc(Loc); }
};

struct FunctionLocInfo {
  SourceLocation LocalRangeBegin;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
  SourceLocation LocalRangeEnd;
};

class FunctionTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, FunctionTypeLoc,
                                               FunctionType, FunctionLocInfo> {
  bool hasExceptionSpec() const {
    if (auto *FPT = dyn_cast<FunctionProtoType>(getTypePtr())) {
      return FPT->hasExceptionSpec();
    }
    return false;
  }

  SourceRange *getExceptionSpecRangePtr() const {
    assert(hasExceptionSpec() && "No exception spec range");
    // After the Info comes the ParmVarDecl array, and after that comes the
    // exception specification information.
    return (SourceRange *)(getParmArray() + getNumParams());
  }

public:
  SourceLocation getLocalRangeBegin() const {
    return getLocalData()->LocalRangeBegin;
  }

  void setLocalRangeBegin(SourceLocation L) {
    getLocalData()->LocalRangeBegin = L;
  }

  SourceLocation getLocalRangeEnd() const {
    return getLocalData()->LocalRangeEnd;
  }

  void setLocalRangeEnd(SourceLocation L) { getLocalData()->LocalRangeEnd = L; }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  SourceRange getExceptionSpecRange() const {
    if (hasExceptionSpec())
      return *getExceptionSpecRangePtr();
    return {};
  }

  void setExceptionSpecRange(SourceRange R) {
    if (hasExceptionSpec())
      *getExceptionSpecRangePtr() = R;
  }

  llvm::ArrayRef<ParmVarDecl *> getParams() const {
    return llvm::ArrayRef(getParmArray(), getNumParams());
  }

  // ParmVarDecls* are stored after Info, one for each parameter.
  ParmVarDecl **getParmArray() const {
    return (ParmVarDecl **)getExtraLocalData();
  }

  unsigned getNumParams() const {
    if (isa<FunctionNoProtoType>(getTypePtr()))
      return 0;
    return cast<FunctionProtoType>(getTypePtr())->getNumParams();
  }

  ParmVarDecl *getParam(unsigned i) const { return getParmArray()[i]; }
  void setParam(unsigned i, ParmVarDecl *VD) { getParmArray()[i] = VD; }

  TypeLoc getReturnLoc() const { return getInnerTypeLoc(); }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLocalRangeBegin(), getLocalRangeEnd());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setLocalRangeBegin(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
    setLocalRangeEnd(Loc);
    for (unsigned i = 0, e = getNumParams(); i != e; ++i)
      setParam(i, nullptr);
    if (hasExceptionSpec())
      setExceptionSpecRange(Loc);
  }

  unsigned getExtraLocalDataSize() const {
    unsigned ExceptSpecSize = hasExceptionSpec() ? sizeof(SourceRange) : 0;
    return (getNumParams() * sizeof(ParmVarDecl *)) + ExceptSpecSize;
  }

  unsigned getExtraLocalDataAlignment() const { return alignof(ParmVarDecl *); }

  QualType getInnerType() const { return getTypePtr()->getReturnType(); }
};

class FunctionProtoTypeLoc
    : public InheritingConcreteTypeLoc<FunctionTypeLoc, FunctionProtoTypeLoc,
                                       FunctionProtoType> {};

class FunctionNoProtoTypeLoc
    : public InheritingConcreteTypeLoc<FunctionTypeLoc, FunctionNoProtoTypeLoc,
                                       FunctionNoProtoType> {};

struct ArrayLocInfo {
  SourceLocation LBracketLoc, RBracketLoc;
  Expr *Size;
};

class ArrayTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, ArrayTypeLoc,
                                            ArrayType, ArrayLocInfo> {
public:
  SourceLocation getLBracketLoc() const { return getLocalData()->LBracketLoc; }

  void setLBracketLoc(SourceLocation Loc) { getLocalData()->LBracketLoc = Loc; }

  SourceLocation getRBracketLoc() const { return getLocalData()->RBracketLoc; }

  void setRBracketLoc(SourceLocation Loc) { getLocalData()->RBracketLoc = Loc; }

  SourceRange getBracketsRange() const {
    return SourceRange(getLBracketLoc(), getRBracketLoc());
  }

  Expr *getSizeExpr() const { return getLocalData()->Size; }

  void setSizeExpr(Expr *Size) { getLocalData()->Size = Size; }

  TypeLoc getElementLoc() const { return getInnerTypeLoc(); }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getLBracketLoc(), getRBracketLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setLBracketLoc(Loc);
    setRBracketLoc(Loc);
    setSizeExpr(nullptr);
  }

  QualType getInnerType() const { return getTypePtr()->getElementType(); }
};

class ConstantArrayTypeLoc
    : public InheritingConcreteTypeLoc<ArrayTypeLoc, ConstantArrayTypeLoc,
                                       ConstantArrayType> {};

class IncompleteArrayTypeLoc
    : public InheritingConcreteTypeLoc<ArrayTypeLoc, IncompleteArrayTypeLoc,
                                       IncompleteArrayType> {};

class VariableArrayTypeLoc
    : public InheritingConcreteTypeLoc<ArrayTypeLoc, VariableArrayTypeLoc,
                                       VariableArrayType> {};

//===----------------------------------------------------------------------===//
//
//  All of these need proper implementations.
//
//===----------------------------------------------------------------------===//

struct VectorTypeLocInfo {
  SourceLocation NameLoc;
};

class VectorTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, VectorTypeLoc,
                                             VectorType, VectorTypeLocInfo> {
public:
  SourceLocation getNameLoc() const { return this->getLocalData()->NameLoc; }

  void setNameLoc(SourceLocation Loc) { this->getLocalData()->NameLoc = Loc; }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getNameLoc(), getNameLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setNameLoc(Loc);
  }

  TypeLoc getElementLoc() const { return getInnerTypeLoc(); }

  QualType getInnerType() const { return this->getTypePtr()->getElementType(); }
};

class ExtVectorTypeLoc
    : public InheritingConcreteTypeLoc<VectorTypeLoc, ExtVectorTypeLoc,
                                       ExtVectorType> {};

struct MatrixTypeLocInfo {
  SourceLocation AttrLoc;
  SourceRange OperandParens;
  Expr *RowOperand;
  Expr *ColumnOperand;
};

class MatrixTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, MatrixTypeLoc,
                                             MatrixType, MatrixTypeLocInfo> {
public:
  SourceLocation getAttrNameLoc() const { return getLocalData()->AttrLoc; }
  void setAttrNameLoc(SourceLocation loc) { getLocalData()->AttrLoc = loc; }

  Expr *getAttrRowOperand() const { return getLocalData()->RowOperand; }
  void setAttrRowOperand(Expr *e) { getLocalData()->RowOperand = e; }

  Expr *getAttrColumnOperand() const { return getLocalData()->ColumnOperand; }
  void setAttrColumnOperand(Expr *e) { getLocalData()->ColumnOperand = e; }

  SourceRange getAttrOperandParensRange() const {
    return getLocalData()->OperandParens;
  }
  void setAttrOperandParensRange(SourceRange range) {
    getLocalData()->OperandParens = range;
  }

  SourceRange getLocalSourceRange() const {
    SourceRange range(getAttrNameLoc());
    range.setEnd(getAttrOperandParensRange().getEnd());
    return range;
  }

  void initializeLocal(TreeContext &Context, SourceLocation loc) {
    setAttrNameLoc(loc);
    setAttrOperandParensRange(loc);
    setAttrRowOperand(nullptr);
    setAttrColumnOperand(nullptr);
  }
};

class ConstantMatrixTypeLoc
    : public InheritingConcreteTypeLoc<MatrixTypeLoc, ConstantMatrixTypeLoc,
                                       ConstantMatrixType> {};

class ComplexTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, ComplexTypeLoc,
                                       ComplexType> {};

struct TypeofLocInfo {
  SourceLocation TypeofLoc;
  SourceLocation LParenLoc;
  SourceLocation RParenLoc;
};

struct TypeOfExprTypeLocInfo : public TypeofLocInfo {};

struct TypeOfTypeLocInfo : public TypeofLocInfo {
  TypeSourceInfo *UnmodifiedTInfo;
};

template <class Derived, class TypeClass, class LocalData = TypeofLocInfo>
class TypeofLikeTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, Derived, TypeClass, LocalData> {
public:
  SourceLocation getTypeofLoc() const {
    return this->getLocalData()->TypeofLoc;
  }

  void setTypeofLoc(SourceLocation Loc) {
    this->getLocalData()->TypeofLoc = Loc;
  }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void setParensRange(SourceRange range) {
    setLParenLoc(range.getBegin());
    setRParenLoc(range.getEnd());
  }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getTypeofLoc(), getRParenLoc());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setTypeofLoc(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }
};

class TypeOfExprTypeLoc
    : public TypeofLikeTypeLoc<TypeOfExprTypeLoc, TypeOfExprType,
                               TypeOfExprTypeLocInfo> {
public:
  Expr *getUnderlyingExpr() const { return getTypePtr()->getUnderlyingExpr(); }

  // Reimplemented to account for the GNU typeof extension
  //     typeof unary-expression
  // where there are no parentheses.
  SourceRange getLocalSourceRange() const;
};

class TypeOfTypeLoc
    : public TypeofLikeTypeLoc<TypeOfTypeLoc, TypeOfType, TypeOfTypeLocInfo> {
public:
  QualType getUnmodifiedType() const {
    return this->getTypePtr()->getUnmodifiedType();
  }

  TypeSourceInfo *getUnmodifiedTInfo() const {
    return this->getLocalData()->UnmodifiedTInfo;
  }

  void setUnmodifiedTInfo(TypeSourceInfo *TI) const {
    this->getLocalData()->UnmodifiedTInfo = TI;
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc);
};

// decltype(expression) abc;
// ~~~~~~~~                  DecltypeLoc
//                    ~      RParenLoc
class DeducedTypeLoc
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, DeducedTypeLoc,
                                       DeducedType> {};

struct AutoTypeLocInfo : TypeSpecLocInfo {
  SourceLocation RParenLoc;
};

class AutoTypeLoc : public ConcreteTypeLoc<DeducedTypeLoc, AutoTypeLoc,
                                           AutoType, AutoTypeLocInfo> {
public:
  AutoTypeKeyword getAutoKeyword() const { return getTypePtr()->getKeyword(); }

  SourceLocation getRParenLoc() const { return getLocalData()->RParenLoc; }
  void setRParenLoc(SourceLocation Loc) { getLocalData()->RParenLoc = Loc; }

  SourceRange getLocalSourceRange() const {
    return {getNameLoc(), getNameLoc()};
  }

  void copy(AutoTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());
    memcpy(Data, Loc.Data, size);
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc);
};

struct ElaboratedLocInfo {
  SourceLocation ElaboratedKWLoc;
};

class ElaboratedTypeLoc
    : public ConcreteTypeLoc<UnqualTypeLoc, ElaboratedTypeLoc, ElaboratedType,
                             ElaboratedLocInfo> {
public:
  SourceLocation getElaboratedKeywordLoc() const {
    return !isEmpty() ? getLocalData()->ElaboratedKWLoc : SourceLocation();
  }

  void setElaboratedKeywordLoc(SourceLocation Loc) {
    if (isEmpty()) {
      assert(Loc.isInvalid());
      return;
    }
    getLocalData()->ElaboratedKWLoc = Loc;
  }

  SourceRange getLocalSourceRange() const {
    if (getElaboratedKeywordLoc().isValid())
      return SourceRange(getElaboratedKeywordLoc());
    return SourceRange();
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc);

  TypeLoc getNamedTypeLoc() const { return getInnerTypeLoc(); }

  QualType getInnerType() const { return getTypePtr()->getNamedType(); }

  bool isEmpty() const {
    return getTypePtr()->getKeyword() == ElaboratedTypeKeyword::None;
  }

  unsigned getLocalDataAlignment() const {
    return ConcreteTypeLoc::getLocalDataAlignment();
  }

  unsigned getLocalDataSize() const {
    return !isEmpty() ? ConcreteTypeLoc::getLocalDataSize() : 0;
  }

  void copy(ElaboratedTypeLoc Loc) {
    unsigned size = getFullDataSize();
    assert(size == Loc.getFullDataSize());
    memcpy(Data, Loc.Data, size);
  }
};

struct AtomicTypeLocInfo {
  SourceLocation KWLoc, LParenLoc, RParenLoc;
};

class AtomicTypeLoc : public ConcreteTypeLoc<UnqualTypeLoc, AtomicTypeLoc,
                                             AtomicType, AtomicTypeLocInfo> {
public:
  TypeLoc getValueLoc() const { return this->getInnerTypeLoc(); }

  SourceRange getLocalSourceRange() const {
    return SourceRange(getKWLoc(), getRParenLoc());
  }

  SourceLocation getKWLoc() const { return this->getLocalData()->KWLoc; }

  void setKWLoc(SourceLocation Loc) { this->getLocalData()->KWLoc = Loc; }

  SourceLocation getLParenLoc() const {
    return this->getLocalData()->LParenLoc;
  }

  void setLParenLoc(SourceLocation Loc) {
    this->getLocalData()->LParenLoc = Loc;
  }

  SourceLocation getRParenLoc() const {
    return this->getLocalData()->RParenLoc;
  }

  void setRParenLoc(SourceLocation Loc) {
    this->getLocalData()->RParenLoc = Loc;
  }

  SourceRange getParensRange() const {
    return SourceRange(getLParenLoc(), getRParenLoc());
  }

  void setParensRange(SourceRange Range) {
    setLParenLoc(Range.getBegin());
    setRParenLoc(Range.getEnd());
  }

  void initializeLocal(TreeContext &Context, SourceLocation Loc) {
    setKWLoc(Loc);
    setLParenLoc(Loc);
    setRParenLoc(Loc);
  }

  QualType getInnerType() const { return this->getTypePtr()->getValueType(); }
};

template <typename T> inline T TypeLoc::getAsAdjusted() const {
  TypeLoc Cur = *this;
  while (!T::isKind(Cur)) {
    if (auto PTL = Cur.getAs<ParenTypeLoc>())
      Cur = PTL.getInnerLoc();
    else if (auto ATL = Cur.getAs<AttributedTypeLoc>())
      Cur = ATL.getModifiedLoc();
    else if (auto ATL = Cur.getAs<BTFTagAttributedTypeLoc>())
      Cur = ATL.getWrappedLoc();
    else if (auto ETL = Cur.getAs<ElaboratedTypeLoc>())
      Cur = ETL.getNamedTypeLoc();
    else if (auto ATL = Cur.getAs<AdjustedTypeLoc>())
      Cur = ATL.getOriginalLoc();
    else if (auto MQL = Cur.getAs<MacroQualifiedTypeLoc>())
      Cur = MQL.getInnerLoc();
    else
      break;
  }
  return Cur.getAs<T>();
}
class BitIntTypeLoc final
    : public InheritingConcreteTypeLoc<TypeSpecTypeLoc, BitIntTypeLoc,
                                       BitIntType> {};
} // namespace neverc

#endif // NEVERC_AST_TYPELOC_H
