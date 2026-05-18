#include "neverc/Tree/Core/APValue.h"
#include "Core/Linkage.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Expr/Expr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace neverc;

// ===----------------------------------------------------------------------===
// APValue implementation
// ===----------------------------------------------------------------------===

APValue::LValueBase::LValueBase(const ValueDecl *P, unsigned I, unsigned V)
    : Ptr(P ? cast<ValueDecl>(P->getCanonicalDecl()) : nullptr), Local{I, V} {}
APValue::LValueBase::LValueBase(const Expr *P, unsigned I, unsigned V)
    : Ptr(P), Local{I, V} {}

QualType APValue::LValueBase::getType() const {
  if (!*this)
    return QualType();
  if (const ValueDecl *D = dyn_cast<const ValueDecl *>()) {
    for (auto *Redecl = cast<ValueDecl>(D->getMostRecentDecl()); Redecl;
         Redecl = cast_or_null<ValueDecl>(Redecl->getPreviousDecl())) {
      QualType T = Redecl->getType();
      if (!T->isIncompleteArrayType())
        return T;
    }
    return D->getType();
  }

  return get<const Expr *>()->getType();
}

unsigned APValue::LValueBase::getCallIndex() const { return Local.CallIndex; }

unsigned APValue::LValueBase::getVersion() const { return Local.Version; }

void APValue::LValueBase::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddPointer(Ptr.getOpaqueValue());
  ID.AddInteger(Local.CallIndex);
  ID.AddInteger(Local.Version);
}

bool neverc::operator==(const APValue::LValueBase &LHS,
                        const APValue::LValueBase &RHS) {
  if (LHS.Ptr != RHS.Ptr)
    return false;
  return LHS.Local.CallIndex == RHS.Local.CallIndex &&
         LHS.Local.Version == RHS.Local.Version;
}

APValue::LValuePathEntry::LValuePathEntry(const Decl *Field) {
  Value =
      reinterpret_cast<uintptr_t>(Field ? Field->getCanonicalDecl() : nullptr);
}

void APValue::LValuePathEntry::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger(Value);
}

namespace {
struct LVBase {
  APValue::LValueBase Base;
  CharUnits Offset;
  unsigned PathLength;
  bool IsNullPtr : 1;
  bool IsOnePastTheEnd : 1;
};
} // namespace

void *APValue::LValueBase::getOpaqueValue() const {
  return Ptr.getOpaqueValue();
}

bool APValue::LValueBase::isNull() const { return Ptr.isNull(); }

APValue::LValueBase::operator bool() const { return static_cast<bool>(Ptr); }

neverc::APValue::LValueBase
llvm::DenseMapInfo<neverc::APValue::LValueBase>::getEmptyKey() {
  neverc::APValue::LValueBase B;
  B.Ptr = DenseMapInfo<const ValueDecl *>::getEmptyKey();
  return B;
}

neverc::APValue::LValueBase
llvm::DenseMapInfo<neverc::APValue::LValueBase>::getTombstoneKey() {
  neverc::APValue::LValueBase B;
  B.Ptr = DenseMapInfo<const ValueDecl *>::getTombstoneKey();
  return B;
}

llvm::hash_code neverc::hash_value(const APValue::LValueBase &Base) {
  return llvm::hash_combine(Base.getOpaqueValue(), Base.getCallIndex(),
                            Base.getVersion());
}

unsigned llvm::DenseMapInfo<neverc::APValue::LValueBase>::getHashValue(
    const neverc::APValue::LValueBase &Base) {
  return hash_value(Base);
}

bool llvm::DenseMapInfo<neverc::APValue::LValueBase>::isEqual(
    const neverc::APValue::LValueBase &LHS,
    const neverc::APValue::LValueBase &RHS) {
  return LHS == RHS;
}

struct APValue::LV : LVBase {
  static const unsigned InlinePathSpace =
      (DataSize - sizeof(LVBase)) / sizeof(LValuePathEntry);

  union {
    LValuePathEntry Path[InlinePathSpace];
    LValuePathEntry *PathPtr;
  };

  LV() { PathLength = (unsigned)-1; }
  ~LV() { resizePath(0); }

  void resizePath(unsigned Length) {
    if (Length == PathLength)
      return;
    if (hasPathPtr())
      delete[] PathPtr;
    PathLength = Length;
    if (hasPathPtr())
      PathPtr = new LValuePathEntry[Length];
  }

  bool hasPath() const { return PathLength != (unsigned)-1; }
  bool hasPathPtr() const { return hasPath() && PathLength > InlinePathSpace; }

  LValuePathEntry *getPath() { return hasPathPtr() ? PathPtr : Path; }
  const LValuePathEntry *getPath() const {
    return hasPathPtr() ? PathPtr : Path;
  }
};

APValue::Arr::Arr(unsigned NumElts, unsigned Size)
    : Elts(new APValue[NumElts + (NumElts != Size ? 1 : 0)]), NumElts(NumElts),
      ArrSize(Size) {}
APValue::Arr::~Arr() { delete[] Elts; }

APValue::StructData::StructData(unsigned NumFields)
    : Elts(new APValue[NumFields]), NumFields(NumFields) {}
APValue::StructData::~StructData() { delete[] Elts; }

APValue::UnionData::UnionData() : Field(nullptr), Value(new APValue) {}
APValue::UnionData::~UnionData() { delete Value; }

APValue::APValue(const APValue &RHS) : Kind(None) {
  switch (RHS.getKind()) {
  case None:
  case Indeterminate:
    Kind = RHS.getKind();
    break;
  case Int:
    MakeInt();
    setInt(RHS.getInt());
    break;
  case Float:
    MakeFloat();
    setFloat(RHS.getFloat());
    break;
  case FixedPoint: {
    APFixedPoint FXCopy = RHS.getFixedPoint();
    MakeFixedPoint(std::move(FXCopy));
    break;
  }
  case Vector:
    MakeVector();
    setVector(((const Vec *)(const char *)&RHS.Data)->Elts,
              RHS.getVectorLength());
    break;
  case ComplexInt:
    MakeComplexInt();
    setComplexInt(RHS.getComplexIntReal(), RHS.getComplexIntImag());
    break;
  case ComplexFloat:
    MakeComplexFloat();
    setComplexFloat(RHS.getComplexFloatReal(), RHS.getComplexFloatImag());
    break;
  case LValue:
    MakeLValue();
    if (RHS.hasLValuePath())
      setLValue(RHS.getLValueBase(), RHS.getLValueOffset(), RHS.getLValuePath(),
                RHS.isLValueOnePastTheEnd(), RHS.isNullPointer());
    else
      setLValue(RHS.getLValueBase(), RHS.getLValueOffset(), NoLValuePath(),
                RHS.isNullPointer());
    break;
  case Array:
    MakeArray(RHS.getArrayInitializedElts(), RHS.getArraySize());
    for (unsigned I = 0, N = RHS.getArrayInitializedElts(); I != N; ++I)
      getArrayInitializedElt(I) = RHS.getArrayInitializedElt(I);
    if (RHS.hasArrayFiller())
      getArrayFiller() = RHS.getArrayFiller();
    break;
  case Struct:
    MakeStruct(RHS.getStructNumFields());
    for (unsigned I = 0, N = RHS.getStructNumFields(); I != N; ++I)
      getStructField(I) = RHS.getStructField(I);
    break;
  case Union:
    MakeUnion();
    setUnion(RHS.getUnionField(), RHS.getUnionValue());
    break;
  case AddrLabelDiff:
    MakeAddrLabelDiff();
    setAddrLabelDiff(RHS.getAddrLabelDiffLHS(), RHS.getAddrLabelDiffRHS());
    break;
  }
}

APValue::APValue(APValue &&RHS) : Kind(RHS.Kind), Data(RHS.Data) {
  RHS.Kind = None;
}

APValue &APValue::operator=(const APValue &RHS) {
  if (this != &RHS)
    *this = APValue(RHS);
  return *this;
}

APValue &APValue::operator=(APValue &&RHS) {
  if (this != &RHS) {
    if (Kind != None && Kind != Indeterminate)
      DestroyDataAndMakeUninit();
    Kind = RHS.Kind;
    Data = RHS.Data;
    RHS.Kind = None;
  }
  return *this;
}

void APValue::DestroyDataAndMakeUninit() {
  if (Kind == Int)
    ((APSInt *)(char *)&Data)->~APSInt();
  else if (Kind == Float)
    ((APFloat *)(char *)&Data)->~APFloat();
  else if (Kind == FixedPoint)
    ((APFixedPoint *)(char *)&Data)->~APFixedPoint();
  else if (Kind == Vector)
    ((Vec *)(char *)&Data)->~Vec();
  else if (Kind == ComplexInt)
    ((ComplexAPSInt *)(char *)&Data)->~ComplexAPSInt();
  else if (Kind == ComplexFloat)
    ((ComplexAPFloat *)(char *)&Data)->~ComplexAPFloat();
  else if (Kind == LValue)
    ((LV *)(char *)&Data)->~LV();
  else if (Kind == Array)
    ((Arr *)(char *)&Data)->~Arr();
  else if (Kind == Struct)
    ((StructData *)(char *)&Data)->~StructData();
  else if (Kind == Union)
    ((UnionData *)(char *)&Data)->~UnionData();
  else if (Kind == AddrLabelDiff)
    ((AddrLabelDiffData *)(char *)&Data)->~AddrLabelDiffData();
  Kind = None;
}

bool APValue::needsCleanup() const {
  switch (getKind()) {
  case None:
  case Indeterminate:
  case AddrLabelDiff:
    return false;
  case Struct:
  case Union:
  case Array:
  case Vector:
    return true;
  case Int:
    return getInt().needsCleanup();
  case Float:
    return getFloat().needsCleanup();
  case FixedPoint:
    return getFixedPoint().getValue().needsCleanup();
  case ComplexFloat:
    assert(getComplexFloatImag().needsCleanup() ==
               getComplexFloatReal().needsCleanup() &&
           "In _Complex float types, real and imaginary values always have the "
           "same size.");
    return getComplexFloatReal().needsCleanup();
  case ComplexInt:
    assert(getComplexIntImag().needsCleanup() ==
               getComplexIntReal().needsCleanup() &&
           "In _Complex int types, real and imaginary values must have the "
           "same size.");
    return getComplexIntReal().needsCleanup();
  case LValue:
    return reinterpret_cast<const LV *>(&Data)->hasPathPtr();
  }
  llvm_unreachable("Unknown APValue kind!");
}

void APValue::swap(APValue &RHS) {
  std::swap(Kind, RHS.Kind);
  std::swap(Data, RHS.Data);
}

namespace {
void profileIntValue(llvm::FoldingSetNodeID &ID, const llvm::APInt &V) {
  for (unsigned I = 0, N = V.getBitWidth(); I < N; I += 32)
    ID.AddInteger((uint32_t)V.extractBitsAsZExtValue(std::min(32u, N - I), I));
}
} // namespace

void APValue::Profile(llvm::FoldingSetNodeID &ID) const {
  // Note that our profiling assumes that only APValues of the same type are
  // ever compared. As a result, we don't consider collisions that could only
  // happen if the types are different. (For example, structs with different
  // numbers of members could profile the same.)

  ID.AddInteger(Kind);

  switch (Kind) {
  case None:
  case Indeterminate:
    return;

  case AddrLabelDiff:
    ID.AddPointer(getAddrLabelDiffLHS()->getLabel()->getCanonicalDecl());
    ID.AddPointer(getAddrLabelDiffRHS()->getLabel()->getCanonicalDecl());
    return;

  case Struct:
    for (unsigned I = 0, N = getStructNumFields(); I != N; ++I)
      getStructField(I).Profile(ID);
    return;

  case Union:
    if (!getUnionField()) {
      ID.AddInteger(0);
      return;
    }
    ID.AddInteger(getUnionField()->getFieldIndex() + 1);
    getUnionValue().Profile(ID);
    return;

  case Array: {
    if (getArraySize() == 0)
      return;

    // The profile should not depend on whether the array is expanded or
    // not, but we don't want to profile the array filler many times for
    // a large array. So treat all equal trailing elements as the filler.
    // Elements are profiled in reverse order to support this, and the
    // first profiled element is followed by a count. For example:
    //
    //   ['a', 'c', 'x', 'x', 'x'] is profiled as
    //   [5, 'x', 3, 'c', 'a']
    llvm::FoldingSetNodeID FillerID;
    (hasArrayFiller() ? getArrayFiller()
                      : getArrayInitializedElt(getArrayInitializedElts() - 1))
        .Profile(FillerID);
    ID.AddNodeID(FillerID);
    unsigned NumFillers = getArraySize() - getArrayInitializedElts();
    unsigned N = getArrayInitializedElts();

    // Count the number of elements equal to the last one. This loop ends
    // by adding an integer indicating the number of such elements, with
    // N set to the number of elements left to profile.
    while (true) {
      if (N == 0) {
        // All elements are fillers.
        assert(NumFillers == getArraySize());
        ID.AddInteger(NumFillers);
        break;
      }

      // No need to check if the last element is equal to the last
      // element.
      if (N != getArraySize()) {
        llvm::FoldingSetNodeID ElemID;
        getArrayInitializedElt(N - 1).Profile(ElemID);
        if (ElemID != FillerID) {
          ID.AddInteger(NumFillers);
          ID.AddNodeID(ElemID);
          --N;
          break;
        }
      }

      // This is a filler.
      ++NumFillers;
      --N;
    }

    for (; N != 0; --N)
      getArrayInitializedElt(N - 1).Profile(ID);
    return;
  }

  case Vector:
    for (unsigned I = 0, N = getVectorLength(); I != N; ++I)
      getVectorElt(I).Profile(ID);
    return;

  case Int:
    profileIntValue(ID, getInt());
    return;

  case Float:
    profileIntValue(ID, getFloat().bitcastToAPInt());
    return;

  case FixedPoint:
    profileIntValue(ID, getFixedPoint().getValue());
    return;

  case ComplexFloat:
    profileIntValue(ID, getComplexFloatReal().bitcastToAPInt());
    profileIntValue(ID, getComplexFloatImag().bitcastToAPInt());
    return;

  case ComplexInt:
    profileIntValue(ID, getComplexIntReal());
    profileIntValue(ID, getComplexIntImag());
    return;

  case LValue:
    getLValueBase().Profile(ID);
    ID.AddInteger(getLValueOffset().getQuantity());
    ID.AddInteger((isNullPointer() ? 1 : 0) |
                  (isLValueOnePastTheEnd() ? 2 : 0) |
                  (hasLValuePath() ? 4 : 0));
    if (hasLValuePath()) {
      ID.AddInteger(getLValuePath().size());
      // For uniqueness, we only need to profile the entries corresponding
      // to union members, but we don't have the type here so we don't know
      // how to interpret the entries.
      for (LValuePathEntry E : getLValuePath())
        E.Profile(ID);
    }
    return;
  }

  llvm_unreachable("Unknown APValue kind!");
}

namespace {
double getApproxValue(const llvm::APFloat &F) {
  llvm::APFloat V = F;
  bool ignored;
  V.convert(llvm::APFloat::IEEEdouble(), llvm::APFloat::rmNearestTiesToEven,
            &ignored);
  return V.convertToDouble();
}

bool tryPrintAsStringLiteral(llvm::raw_ostream &Out,
                             const PrintingPolicy &Policy, const ArrayType *ATy,
                             llvm::ArrayRef<APValue> Inits) {
  if (Inits.empty())
    return false;

  QualType Ty = ATy->getElementType();
  if (!Ty->isAnyCharacterType())
    return false;

  // Nothing we can do about a sequence that is not null-terminated
  if (!Inits.back().isInt() || !Inits.back().getInt().isZero())
    return false;

  Inits = Inits.drop_back();

  llvm::SmallString<40> Buf;
  Buf.push_back('"');

  // Better than printing a two-digit sequence of 10 integers.
  constexpr size_t MaxN = 36;
  llvm::StringRef Ellipsis;
  if (Inits.size() > MaxN && !Policy.EntireContentsOfLargeArray) {
    Ellipsis = "[...]";
    Inits =
        Inits.take_front(std::min(MaxN - Ellipsis.size() / 2, Inits.size()));
  }

  for (auto &Val : Inits) {
    if (!Val.isInt())
      return false;
    int64_t Char64 = Val.getInt().getExtValue();
    if (!isASCII(Char64))
      return false; // Bye bye, see you in integers.
    auto Ch = static_cast<unsigned char>(Char64);
    // The diagnostic message is 'quoted'
    llvm::StringRef Escaped = escapeCStyle<EscapeChar::SingleAndDouble>(Ch);
    if (Escaped.empty()) {
      if (!isPrintable(Ch))
        return false;
      Buf.emplace_back(Ch);
    } else {
      Buf.append(Escaped);
    }
  }

  Buf.append(Ellipsis);
  Buf.push_back('"');

  if (Ty->isWideCharType())
    Out << 'L';
  else if (Ty->isChar8Type())
    Out << "u8";
  else if (Ty->isChar16Type())
    Out << 'u';
  else if (Ty->isChar32Type())
    Out << 'U';

  Out << Buf;
  return true;
}
} // namespace

void APValue::printPretty(llvm::raw_ostream &Out, const TreeContext &Ctx,
                          QualType Ty) const {
  printPretty(Out, Ctx.getPrintingPolicy(), Ty, &Ctx);
}

void APValue::printPretty(llvm::raw_ostream &Out, const PrintingPolicy &Policy,
                          QualType Ty, const TreeContext *Ctx) const {
  // There are no objects of type 'void', but values of this type can be
  // returned from functions.
  if (Ty->isVoidType()) {
    Out << "void()";
    return;
  }

  switch (getKind()) {
  case APValue::None:
    Out << "<out of lifetime>";
    return;
  case APValue::Indeterminate:
    Out << "<uninitialized>";
    return;
  case APValue::Int:
    if (Ty->isBooleanType())
      Out << (getInt().getBoolValue() ? "true" : "false");
    else
      Out << getInt();
    return;
  case APValue::Float:
    Out << getApproxValue(getFloat());
    return;
  case APValue::FixedPoint:
    Out << getFixedPoint();
    return;
  case APValue::Vector: {
    Out << '{';
    QualType ElemTy = Ty->castAs<VectorType>()->getElementType();
    getVectorElt(0).printPretty(Out, Policy, ElemTy, Ctx);
    for (unsigned i = 1; i != getVectorLength(); ++i) {
      Out << ", ";
      getVectorElt(i).printPretty(Out, Policy, ElemTy, Ctx);
    }
    Out << '}';
    return;
  }
  case APValue::ComplexInt:
    Out << getComplexIntReal() << "+" << getComplexIntImag() << "i";
    return;
  case APValue::ComplexFloat:
    Out << getApproxValue(getComplexFloatReal()) << "+"
        << getApproxValue(getComplexFloatImag()) << "i";
    return;
  case APValue::LValue: {
    QualType InnerTy = Ty->getPointeeType();
    if (InnerTy.isNull())
      InnerTy = Ty;

    LValueBase Base = getLValueBase();
    if (!Base) {
      if (isNullPointer()) {
        Out << (Policy.Nullptr ? "nullptr" : "0");
      } else {
        Out << "(" << Ty.stream(Policy) << ")"
            << getLValueOffset().getQuantity();
      }
      return;
    }

    if (!hasLValuePath()) {
      CharUnits O = getLValueOffset();
      CharUnits S = Ctx ? Ctx->getTypeSizeInCharsIfKnown(InnerTy).value_or(
                              CharUnits::Zero())
                        : CharUnits::Zero();
      if (!O.isZero()) {
        if (S.isZero() || O % S) {
          Out << "(char*)";
          S = CharUnits::One();
        }
        Out << '&';
      } else {
        Out << '&';
      }

      if (const ValueDecl *VD = Base.dyn_cast<const ValueDecl *>())
        Out << *VD;
      else {
        assert(Base.get<const Expr *>() != nullptr &&
               "Expecting non-null Expr");
        Base.get<const Expr *>()->printPretty(Out, nullptr, Policy);
      }

      if (!O.isZero())
        Out << " + " << (O / S);
      return;
    }

    Out << '&';

    QualType ElemTy = Base.getType();
    if (const ValueDecl *VD = Base.dyn_cast<const ValueDecl *>()) {
      Out << *VD;
    } else {
      const Expr *E = Base.get<const Expr *>();
      assert(E != nullptr && "Expecting non-null Expr");
      E->printPretty(Out, nullptr, Policy);
    }

    llvm::ArrayRef<LValuePathEntry> Path = getLValuePath();
    for (unsigned I = 0, N = Path.size(); I != N; ++I) {
      if (ElemTy->isRecordType()) {
        const ValueDecl *VD = cast<ValueDecl>(Path[I].getAsField());
        Out << "." << *VD;
        ElemTy = VD->getType();
      } else if (ElemTy->isAnyComplexType()) {
        // The lvalue refers to a complex type
        Out << (Path[I].getAsArrayIndex() == 0 ? ".real" : ".imag");
        ElemTy = ElemTy->castAs<ComplexType>()->getElementType();
      } else {
        // The lvalue must refer to an array.
        Out << '[' << Path[I].getAsArrayIndex() << ']';
        ElemTy = ElemTy->castAsArrayTypeUnsafe()->getElementType();
      }
    }

    if (isLValueOnePastTheEnd())
      Out << " + 1";
    return;
  }
  case APValue::Array: {
    const ArrayType *AT = Ty->castAsArrayTypeUnsafe();
    unsigned N = getArrayInitializedElts();
    if (N != 0 && tryPrintAsStringLiteral(Out, Policy, AT,
                                          {&getArrayInitializedElt(0), N}))
      return;
    QualType ElemTy = AT->getElementType();
    Out << '{';
    unsigned I = 0;
    switch (N) {
    case 0:
      for (; I != N; ++I) {
        Out << ", ";
        if (I == 10 && !Policy.EntireContentsOfLargeArray) {
          Out << "...}";
          return;
        }
        [[fallthrough]];
      default:
        getArrayInitializedElt(I).printPretty(Out, Policy, ElemTy, Ctx);
      }
    }
    Out << '}';
    return;
  }
  case APValue::Struct: {
    Out << '{';
    const RecordDecl *RD = Ty->castAs<RecordType>()->getDecl();
    bool First = true;
    for (const auto *FI : RD->fields()) {
      if (!First)
        Out << ", ";
      if (FI->isUnnamedBitfield())
        continue;
      getStructField(FI->getFieldIndex())
          .printPretty(Out, Policy, FI->getType(), Ctx);
      First = false;
    }
    Out << '}';
    return;
  }
  case APValue::Union:
    Out << '{';
    if (const FieldDecl *FD = getUnionField()) {
      Out << "." << *FD << " = ";
      getUnionValue().printPretty(Out, Policy, FD->getType(), Ctx);
    }
    Out << '}';
    return;
  case APValue::AddrLabelDiff:
    Out << "&&" << getAddrLabelDiffLHS()->getLabel()->getName();
    Out << " - ";
    Out << "&&" << getAddrLabelDiffRHS()->getLabel()->getName();
    return;
  }
  llvm_unreachable("Unknown APValue kind!");
}

std::string APValue::getAsString(const TreeContext &Ctx, QualType Ty) const {
  std::string Result;
  llvm::raw_string_ostream Out(Result);
  printPretty(Out, Ctx, Ty);
  Out.flush();
  return Result;
}

bool APValue::toIntegralConstant(APSInt &Result, QualType SrcTy,
                                 const TreeContext &Ctx) const {
  if (isInt()) {
    Result = getInt();
    return true;
  }

  if (isLValue() && isNullPointer()) {
    Result = Ctx.MakeIntValue(Ctx.getTargetNullPointerValue(SrcTy), SrcTy);
    return true;
  }

  if (isLValue() && !getLValueBase()) {
    Result = Ctx.MakeIntValue(getLValueOffset().getQuantity(), SrcTy);
    return true;
  }

  return false;
}

const APValue::LValueBase APValue::getLValueBase() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV *)(const void *)&Data)->Base;
}

bool APValue::isLValueOnePastTheEnd() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV *)(const void *)&Data)->IsOnePastTheEnd;
}

CharUnits &APValue::getLValueOffset() {
  assert(isLValue() && "Invalid accessor");
  return ((LV *)(void *)&Data)->Offset;
}

bool APValue::hasLValuePath() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV *)(const char *)&Data)->hasPath();
}

llvm::ArrayRef<APValue::LValuePathEntry> APValue::getLValuePath() const {
  assert(isLValue() && hasLValuePath() && "Invalid accessor");
  const LV &LVal = *((const LV *)(const char *)&Data);
  return llvm::ArrayRef(LVal.getPath(), LVal.PathLength);
}

unsigned APValue::getLValueCallIndex() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV *)(const char *)&Data)->Base.getCallIndex();
}

unsigned APValue::getLValueVersion() const {
  assert(isLValue() && "Invalid accessor");
  return ((const LV *)(const char *)&Data)->Base.getVersion();
}

bool APValue::isNullPointer() const {
  assert(isLValue() && "Invalid usage");
  return ((const LV *)(const char *)&Data)->IsNullPtr;
}

void APValue::setLValue(LValueBase B, const CharUnits &O, NoLValuePath,
                        bool IsNullPtr) {
  assert(isLValue() && "Invalid accessor");
  LV &LVal = *((LV *)(char *)&Data);
  LVal.Base = B;
  LVal.IsOnePastTheEnd = false;
  LVal.Offset = O;
  LVal.resizePath((unsigned)-1);
  LVal.IsNullPtr = IsNullPtr;
}

llvm::MutableArrayRef<APValue::LValuePathEntry>
APValue::setLValueUninit(LValueBase B, const CharUnits &O, unsigned Size,
                         bool IsOnePastTheEnd, bool IsNullPtr) {
  assert(isLValue() && "Invalid accessor");
  LV &LVal = *((LV *)(char *)&Data);
  LVal.Base = B;
  LVal.IsOnePastTheEnd = IsOnePastTheEnd;
  LVal.Offset = O;
  LVal.IsNullPtr = IsNullPtr;
  LVal.resizePath(Size);
  return {LVal.getPath(), Size};
}

void APValue::setLValue(LValueBase B, const CharUnits &O,
                        llvm::ArrayRef<LValuePathEntry> Path,
                        bool IsOnePastTheEnd, bool IsNullPtr) {
  llvm::MutableArrayRef<APValue::LValuePathEntry> InternalPath =
      setLValueUninit(B, O, Path.size(), IsOnePastTheEnd, IsNullPtr);
  if (Path.size()) {
    memcpy(InternalPath.data(), Path.data(),
           Path.size() * sizeof(LValuePathEntry));
  }
}

void APValue::setUnion(const FieldDecl *Field, const APValue &Value) {
  assert(isUnion() && "Invalid accessor");
  ((UnionData *)(char *)&Data)->Field =
      Field ? Field->getCanonicalDecl() : nullptr;
  *((UnionData *)(char *)&Data)->Value = Value;
}

void APValue::MakeLValue() {
  assert(isAbsent() && "Bad state change");
  static_assert(sizeof(LV) <= DataSize, "LV too big");
  new ((void *)(char *)&Data) LV();
  Kind = LValue;
}

void APValue::MakeArray(unsigned InitElts, unsigned Size) {
  assert(isAbsent() && "Bad state change");
  new ((void *)(char *)&Data) Arr(InitElts, Size);
  Kind = Array;
}

llvm::MutableArrayRef<APValue::LValuePathEntry>
setLValueUninit(APValue::LValueBase B, const CharUnits &O, unsigned Size,
                bool OnePastTheEnd, bool IsNullPtr);

LinkageInfo LinkageComputer::getLVForValue(const APValue &V,
                                           LVComputationKind computation) {
  LinkageInfo LV = LinkageInfo::external();

  auto MergeLV = [&](LinkageInfo MergeLV) {
    LV.merge(MergeLV);
    return LV.getLinkage() == Linkage::Internal;
  };
  auto Merge = [&](const APValue &V) {
    return MergeLV(getLVForValue(V, computation));
  };

  switch (V.getKind()) {
  case APValue::None:
  case APValue::Indeterminate:
  case APValue::Int:
  case APValue::Float:
  case APValue::FixedPoint:
  case APValue::ComplexInt:
  case APValue::ComplexFloat:
  case APValue::Vector:
    break;

  case APValue::AddrLabelDiff:
    // Even for an inline function, it's not reasonable to treat a difference
    // between the addresses of labels as an external value.
    return LinkageInfo::internal();

  case APValue::Struct: {
    for (unsigned I = 0, N = V.getStructNumFields(); I != N; ++I)
      if (Merge(V.getStructField(I)))
        break;
    break;
  }

  case APValue::Union:
    if (V.getUnionField())
      Merge(V.getUnionValue());
    break;

  case APValue::Array: {
    for (unsigned I = 0, N = V.getArrayInitializedElts(); I != N; ++I)
      if (Merge(V.getArrayInitializedElt(I)))
        break;
    if (V.hasArrayFiller())
      Merge(V.getArrayFiller());
    break;
  }

  case APValue::LValue: {
    if (!V.getLValueBase()) {
      // Null or absolute address: this is external.
    } else if (const auto *VD =
                   V.getLValueBase().dyn_cast<const ValueDecl *>()) {
      if (VD && MergeLV(getLVForDecl(VD, computation)))
        break;
    } else if (V.getLValueBase().is<const Expr *>()) {
      return LinkageInfo::internal();
    }
    // The lvalue path doesn't matter: pointers to all subobjects always have
    // the same visibility as pointers to the complete object.
    break;
  }
  }

  return LV;
}
