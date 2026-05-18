#include "neverc/Tree/Type/TypeLoc.h"
#include "neverc/Tree/Core/Attr.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Type/TypeLocVisitor.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstring>

using namespace neverc;

// ===----------------------------------------------------------------------===
// Type location management
// ===----------------------------------------------------------------------===

namespace {
const unsigned TypeLocMaxDataAlign = alignof(void *);

namespace {

class TypeLocRanger : public TypeLocVisitor<TypeLocRanger, SourceRange> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  SourceRange Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) {                    \
    return TyLoc.getLocalSourceRange();                                        \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace
} // namespace

SourceRange TypeLoc::getLocalSourceRangeImpl(TypeLoc TL) {
  if (TL.isNull())
    return SourceRange();
  return TypeLocRanger().Visit(TL);
}

namespace {

class TypeAligner : public TypeLocVisitor<TypeAligner, unsigned> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  unsigned Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) {                       \
    return TyLoc.getLocalDataAlignment();                                      \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace

unsigned TypeLoc::getLocalAlignmentForType(QualType Ty) {
  if (Ty.isNull())
    return 1;
  return TypeAligner().Visit(TypeLoc(Ty, nullptr));
}

namespace {

class TypeSizer : public TypeLocVisitor<TypeSizer, unsigned> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  unsigned Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) {                       \
    return TyLoc.getLocalDataSize();                                           \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace

unsigned TypeLoc::getFullDataSizeForType(QualType Ty) {
  unsigned Total = 0;
  TypeLoc TyLoc(Ty, nullptr);
  unsigned MaxAlign = 1;
  while (!TyLoc.isNull()) {
    unsigned Align = getLocalAlignmentForType(TyLoc.getType());
    MaxAlign = std::max(Align, MaxAlign);
    Total = llvm::alignTo(Total, Align);
    Total += TypeSizer().Visit(TyLoc);
    TyLoc = TyLoc.getNextTypeLoc();
  }
  Total = llvm::alignTo(Total, MaxAlign);
  return Total;
}

namespace {

class NextLoc : public TypeLocVisitor<NextLoc, TypeLoc> {
public:
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  TypeLoc Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) {                        \
    return TyLoc.getNextTypeLoc();                                             \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace

TypeLoc TypeLoc::getNextTypeLocImpl(TypeLoc TL) { return NextLoc().Visit(TL); }

void TypeLoc::initializeImpl(TreeContext &Context, TypeLoc TL,
                             SourceLocation Loc) {
  while (true) {
    switch (TL.getTypeLocClass()) {
#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  case CLASS: {                                                                \
    CLASS##TypeLoc TLCasted = TL.castAs<CLASS##TypeLoc>();                     \
    TLCasted.initializeLocal(Context, Loc);                                    \
    TL = TLCasted.getNextTypeLoc();                                            \
    if (!TL)                                                                   \
      return;                                                                  \
    continue;                                                                  \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
    }
  }
}

namespace {

class TypeLocCopier : public TypeLocVisitor<TypeLocCopier> {
  TypeLoc Source;

public:
  TypeLocCopier(TypeLoc source) : Source(source) {}

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  void Visit##CLASS##TypeLoc(CLASS##TypeLoc dest) {                            \
    dest.copyLocal(Source.castAs<CLASS##TypeLoc>());                           \
  }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace

void TypeLoc::copy(TypeLoc other) {
  assert(getFullDataSize() == other.getFullDataSize());

  // If both data pointers are aligned to the maximum alignment, we
  // can memcpy because getFullDataSize() accurately reflects the
  // layout of the data.
  if (reinterpret_cast<uintptr_t>(Data) ==
          llvm::alignTo(reinterpret_cast<uintptr_t>(Data),
                        TypeLocMaxDataAlign) &&
      reinterpret_cast<uintptr_t>(other.Data) ==
          llvm::alignTo(reinterpret_cast<uintptr_t>(other.Data),
                        TypeLocMaxDataAlign)) {
    memcpy(Data, other.Data, getFullDataSize());
    return;
  }

  // Copy each of the pieces.
  TypeLoc TL(getType(), Data);
  do {
    TypeLocCopier(other).Visit(TL);
    other = other.getNextTypeLoc();
  } while ((TL = TL.getNextTypeLoc()));
}

SourceLocation TypeLoc::getBeginLoc() const {
  TypeLoc Cur = *this;
  TypeLoc LeftMost = Cur;
  while (true) {
    switch (Cur.getTypeLocClass()) {
    case Elaborated:
      if (Cur.getLocalSourceRange().getBegin().isValid()) {
        LeftMost = Cur;
        break;
      }
      Cur = Cur.getNextTypeLoc();
      if (Cur.isNull())
        break;
      continue;
    case FunctionProto:
    case FunctionNoProto:
    case ConstantArray:
    case IncompleteArray:
    case VariableArray:
    case Qualified:
      Cur = Cur.getNextTypeLoc();
      continue;
    default:
      if (Cur.getLocalSourceRange().getBegin().isValid())
        LeftMost = Cur;
      Cur = Cur.getNextTypeLoc();
      if (Cur.isNull())
        break;
      continue;
    } // switch
    break;
  } // while
  return LeftMost.getLocalSourceRange().getBegin();
}

SourceLocation TypeLoc::getEndLoc() const {
  TypeLoc Cur = *this;
  TypeLoc Last;
  while (true) {
    switch (Cur.getTypeLocClass()) {
    default:
      if (!Last)
        Last = Cur;
      return Last.getLocalSourceRange().getEnd();
    case Paren:
    case ConstantArray:
    case IncompleteArray:
    case VariableArray:
    case FunctionNoProto:
      // The innermost type with suffix syntax always determines the end of the
      // type.
      Last = Cur;
      break;
    case FunctionProto:
      Last = Cur;
      break;
    case Pointer:
      // Types with prefix syntax only determine the end of the type if there
      // is no suffix type.
      if (!Last)
        Last = Cur;
      break;
    case Qualified:
    case Elaborated:
      break;
    }
    Cur = Cur.getNextTypeLoc();
  }
}

namespace {

struct TSTChecker : public TypeLocVisitor<TSTChecker, bool> {
  // Overload resolution does the real work for us.
  static bool isTypeSpec(TypeSpecTypeLoc _) { return true; }
  static bool isTypeSpec(TypeLoc _) { return false; }

#define ABSTRACT_TYPELOC(CLASS, PARENT)
#define TYPELOC(CLASS, PARENT)                                                 \
  bool Visit##CLASS##TypeLoc(CLASS##TypeLoc TyLoc) { return isTypeSpec(TyLoc); }
#include "neverc/Tree/Type/TypeLocNodes.def"
};

} // namespace

bool TypeSpecTypeLoc::isKind(const TypeLoc &TL) {
  if (TL.getType().hasLocalQualifiers())
    return false;
  return TSTChecker().Visit(TL);
}

bool TagTypeLoc::isDefinition() const {
  TagDecl *D = getDecl();
  return D->isCompleteDefinition() &&
         (D->getIdentifier() == nullptr || D->getLocation() == getNameLoc());
}

// Reimplemented to account for GNU extension
//     typeof unary-expression
// where there are no parentheses.
SourceRange TypeOfExprTypeLoc::getLocalSourceRange() const {
  if (getRParenLoc().isValid())
    return SourceRange(getTypeofLoc(), getRParenLoc());
  else
    return SourceRange(getTypeofLoc(),
                       getUnderlyingExpr()->getSourceRange().getEnd());
}

TypeSpecifierType BuiltinTypeLoc::getWrittenTypeSpec() const {
  if (needsExtraLocalData())
    return static_cast<TypeSpecifierType>(getWrittenBuiltinSpecs().Type);
  switch (getTypePtr()->getKind()) {
  case BuiltinType::Void:
    return TST_void;
  case BuiltinType::Bool:
    return TST_bool;
  case BuiltinType::Char_U:
  case BuiltinType::Char_S:
    return TST_char;
  case BuiltinType::Char8:
    return TST_char8;
  case BuiltinType::Char16:
    return TST_char16;
  case BuiltinType::Char32:
    return TST_char32;
  case BuiltinType::WChar_S:
  case BuiltinType::WChar_U:
    return TST_wchar;
  case BuiltinType::UChar:
  case BuiltinType::UShort:
  case BuiltinType::UInt:
  case BuiltinType::ULong:
  case BuiltinType::ULongLong:
  case BuiltinType::UInt128:
  case BuiltinType::SChar:
  case BuiltinType::Short:
  case BuiltinType::Int:
  case BuiltinType::Long:
  case BuiltinType::LongLong:
  case BuiltinType::Int128:
  case BuiltinType::Half:
  case BuiltinType::Float:
  case BuiltinType::Double:
  case BuiltinType::LongDouble:
  case BuiltinType::Float16:
  case BuiltinType::Float128:
  case BuiltinType::ShortAccum:
  case BuiltinType::Accum:
  case BuiltinType::LongAccum:
  case BuiltinType::UShortAccum:
  case BuiltinType::UAccum:
  case BuiltinType::ULongAccum:
  case BuiltinType::ShortFract:
  case BuiltinType::Fract:
  case BuiltinType::LongFract:
  case BuiltinType::UShortFract:
  case BuiltinType::UFract:
  case BuiltinType::ULongFract:
  case BuiltinType::SatShortAccum:
  case BuiltinType::SatAccum:
  case BuiltinType::SatLongAccum:
  case BuiltinType::SatUShortAccum:
  case BuiltinType::SatUAccum:
  case BuiltinType::SatULongAccum:
  case BuiltinType::SatShortFract:
  case BuiltinType::SatFract:
  case BuiltinType::SatLongFract:
  case BuiltinType::SatUShortFract:
  case BuiltinType::SatUFract:
  case BuiltinType::SatULongFract:
  case BuiltinType::BFloat16:
    llvm_unreachable("Builtin type needs extra local data!");
    // Fall through, if the impossible happens.

  case BuiltinType::NullPtr:
  case BuiltinType::Overload:
  case BuiltinType::Dependent:
  case BuiltinType::PseudoObject:
#define SVE_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "neverc/Foundation/Builtin/AArch64SVEACLETypes.def"
  case BuiltinType::BuiltinFn:
  case BuiltinType::IncompleteMatrixIdx:
    return TST_unspecified;
  }

  llvm_unreachable("Invalid BuiltinType Kind!");
}

TypeLoc TypeLoc::IgnoreParensImpl(TypeLoc TL) {
  while (ParenTypeLoc PTL = TL.getAs<ParenTypeLoc>())
    TL = PTL.getInnerLoc();
  return TL;
}

SourceLocation TypeLoc::findNullabilityLoc() const {
  if (auto ATL = getAs<AttributedTypeLoc>()) {
    const Attr *A = ATL.getAttr();
    if (A && (isa<TypeNullableAttr>(A) || isa<TypeNonNullAttr>(A) ||
              isa<TypeNullUnspecifiedAttr>(A)))
      return A->getLocation();
  }

  return {};
}

TypeLoc TypeLoc::findExplicitQualifierLoc() const {
  // Qualified types.
  if (auto qual = getAs<QualifiedTypeLoc>())
    return qual;

  TypeLoc loc = IgnoreParens();

  // Attributed types.
  if (auto attr = loc.getAs<AttributedTypeLoc>()) {
    if (attr.isQualifier())
      return attr;
    return attr.getModifiedLoc().findExplicitQualifierLoc();
  }

  // C11 _Atomic types.
  if (auto atomic = loc.getAs<AtomicTypeLoc>()) {
    return atomic;
  }

  return {};
}

SourceRange AttributedTypeLoc::getLocalSourceRange() const {
  // Note that this does *not* include the range of the attribute
  // enclosure, e.g.:
  //    __attribute__((foo(bar)))
  //    ^~~~~~~~~~~~~~~        ~~
  // or
  //    [[foo(bar)]]
  //    ^~        ~~
  // That enclosure doesn't necessarily belong to a single attribute
  // anyway.
  return getAttr() ? getAttr()->getRange() : SourceRange();
}

SourceRange BTFTagAttributedTypeLoc::getLocalSourceRange() const {
  return getAttr() ? getAttr()->getRange() : SourceRange();
}

void TypeOfTypeLoc::initializeLocal(TreeContext &Context, SourceLocation Loc) {
  TypeofLikeTypeLoc<TypeOfTypeLoc, TypeOfType,
                    TypeOfTypeLocInfo>::initializeLocal(Context, Loc);
  this->getLocalData()->UnmodifiedTInfo =
      Context.getTrivialTypeSourceInfo(getUnmodifiedType(), Loc);
}

void ElaboratedTypeLoc::initializeLocal(TreeContext &Context,
                                        SourceLocation Loc) {
  if (isEmpty())
    return;
  setElaboratedKeywordLoc(Loc);
}

void AutoTypeLoc::initializeLocal(TreeContext &Context, SourceLocation Loc) {
  setRParenLoc(Loc);
  setNameLoc(Loc);
  (void)Context;
}

namespace {

class GetContainedAutoTypeLocVisitor
    : public TypeLocVisitor<GetContainedAutoTypeLocVisitor, TypeLoc> {
public:
  using TypeLocVisitor<GetContainedAutoTypeLocVisitor, TypeLoc>::Visit;

  TypeLoc VisitAutoTypeLoc(AutoTypeLoc TL) { return TL; }

  // Only these types can contain the desired 'auto' type.

  TypeLoc VisitElaboratedTypeLoc(ElaboratedTypeLoc T) {
    return Visit(T.getNamedTypeLoc());
  }

  TypeLoc VisitQualifiedTypeLoc(QualifiedTypeLoc T) {
    return Visit(T.getUnqualifiedLoc());
  }

  TypeLoc VisitPointerTypeLoc(PointerTypeLoc T) {
    return Visit(T.getPointeeLoc());
  }

  TypeLoc VisitArrayTypeLoc(ArrayTypeLoc T) { return Visit(T.getElementLoc()); }

  TypeLoc VisitFunctionTypeLoc(FunctionTypeLoc T) {
    return Visit(T.getReturnLoc());
  }

  TypeLoc VisitParenTypeLoc(ParenTypeLoc T) { return Visit(T.getInnerLoc()); }

  TypeLoc VisitAttributedTypeLoc(AttributedTypeLoc T) {
    return Visit(T.getModifiedLoc());
  }

  TypeLoc VisitBTFTagAttributedTypeLoc(BTFTagAttributedTypeLoc T) {
    return Visit(T.getWrappedLoc());
  }

  TypeLoc VisitMacroQualifiedTypeLoc(MacroQualifiedTypeLoc T) {
    return Visit(T.getInnerLoc());
  }

  TypeLoc VisitAdjustedTypeLoc(AdjustedTypeLoc T) {
    return Visit(T.getOriginalLoc());
  }
};

} // namespace

AutoTypeLoc TypeLoc::getContainedAutoTypeLoc() const {
  TypeLoc Res = GetContainedAutoTypeLocVisitor().Visit(*this);
  if (Res.isNull())
    return AutoTypeLoc();
  return Res.getAs<AutoTypeLoc>();
}
