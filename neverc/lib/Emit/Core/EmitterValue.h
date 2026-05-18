#ifndef NEVERC_LIB_CODEGEN_CORE_CGVALUE_H
#define NEVERC_LIB_CODEGEN_CORE_CGVALUE_H

#include "Core/Address.h"
#include "Core/TBAAEmitter.h"
#include "neverc/Tree/Core/TreeContext.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace llvm {
class Constant;
} // namespace llvm

namespace neverc {
namespace Emit {
class AggValueSlot;
class FunctionEmitter;
struct BitFieldInfo;

class RValue {
  enum Flavor { Scalar, Complex, Aggregate };

  // The shift to make to an aggregate's alignment to make it look
  // like a pointer.
  enum { AggAlignShift = 4 };

  // Stores first value and flavor.
  llvm::PointerIntPair<llvm::Value *, 2, Flavor> V1;
  // Stores second value and volatility.
  llvm::PointerIntPair<llvm::Value *, 1, bool> V2;
  // Stores element type for aggregate values.
  llvm::Type *ElementType;

public:
  bool isScalar() const { return V1.getInt() == Scalar; }
  bool isComplex() const { return V1.getInt() == Complex; }
  bool isAggregate() const { return V1.getInt() == Aggregate; }

  bool isVolatileQualified() const { return V2.getInt(); }

  llvm::Value *getScalarVal() const {
    assert(isScalar() && "Not a scalar!");
    return V1.getPointer();
  }

  std::pair<llvm::Value *, llvm::Value *> getComplexVal() const {
    return std::make_pair(V1.getPointer(), V2.getPointer());
  }

  Address getAggregateAddress() const {
    assert(isAggregate() && "Not an aggregate!");
    auto align = reinterpret_cast<uintptr_t>(V2.getPointer()) >> AggAlignShift;
    return Address(V1.getPointer(), ElementType,
                   CharUnits::fromQuantity(align));
  }
  llvm::Value *getAggregatePointer() const {
    assert(isAggregate() && "Not an aggregate!");
    return V1.getPointer();
  }

  static RValue getIgnored() { return get(nullptr); }

  static RValue get(llvm::Value *V) {
    RValue ER;
    ER.V1.setPointer(V);
    ER.V1.setInt(Scalar);
    ER.V2.setInt(false);
    return ER;
  }
  static RValue getComplex(llvm::Value *V1, llvm::Value *V2) {
    RValue ER;
    ER.V1.setPointer(V1);
    ER.V2.setPointer(V2);
    ER.V1.setInt(Complex);
    ER.V2.setInt(false);
    return ER;
  }
  static RValue getComplex(const std::pair<llvm::Value *, llvm::Value *> &C) {
    return getComplex(C.first, C.second);
  }
  static RValue getAggregate(Address addr, bool isVolatile = false) {
    RValue ER;
    ER.V1.setPointer(addr.getPointer());
    ER.V1.setInt(Aggregate);
    ER.ElementType = addr.getElementType();

    auto align = static_cast<uintptr_t>(addr.getAlignment().getQuantity());
    ER.V2.setPointer(reinterpret_cast<llvm::Value *>(align << AggAlignShift));
    ER.V2.setInt(isVolatile);
    return ER;
  }
};

enum class AlignmentSource {
  Decl,

  AttributedType,

  Type
};

static inline AlignmentSource getFieldAlignmentSource(AlignmentSource Source) {
  // For now, we don't distinguish fields of opaque pointers from
  // top-level declarations, but maybe we should.
  return AlignmentSource::Decl;
}

class LValueBaseInfo {
  AlignmentSource AlignSource;

public:
  explicit LValueBaseInfo(AlignmentSource Source = AlignmentSource::Type)
      : AlignSource(Source) {}
  AlignmentSource getAlignmentSource() const { return AlignSource; }
  void setAlignmentSource(AlignmentSource Source) { AlignSource = Source; }

  void mergeForCast(const LValueBaseInfo &Info) {
    setAlignmentSource(Info.getAlignmentSource());
  }
};

class LValue {
  enum {
    Simple,       // This is a normal l-value, use getAddress().
    VectorElt,    // This is a vector element l-value (V[i]), use getVector*
    BitField,     // This is a bitfield l-value, use getBitfield*.
    ExtVectorElt, // This is an extended vector subset, use getExtVectorComp
    GlobalReg,    // This is a register l-value, use getGlobalReg()
    MatrixElt     // This is a matrix element, use getVector*
  } LVType;

  llvm::Value *V;
  llvm::Type *ElementType;

  union {
    // Index into a vector subscript: V[i]
    llvm::Value *VectorIdx;

    // ExtVector element subset: V.xyx
    llvm::Constant *VectorElts;

    // BitField start bit and size
    const BitFieldInfo *BitFieldData;
  };

  QualType Type;

  // 'const' is unused here
  Qualifiers Quals;

  // The alignment to use when accessing this lvalue.  (For vector elements,
  // this is the alignment of the whole vector.)
  unsigned Alignment;

  bool ThreadLocalRef : 1;

  // This flag shows if a nontemporal load/stores should be used when accessing
  // this lvalue.
  bool Nontemporal : 1;

  // The pointer is known not to be null.
  bool IsKnownNonNull : 1;

  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;

private:
  void Initialize(QualType Type, Qualifiers Quals, CharUnits Alignment,
                  LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo) {
    assert((!Alignment.isZero() || Type->isIncompleteType()) &&
           "initializing l-value with zero alignment!");
    if (isGlobalReg())
      assert(ElementType == nullptr && "Global reg does not store elem type");
    else
      assert(ElementType != nullptr && "Must have elem type");

    this->Type = Type;
    this->Quals = Quals;
    const unsigned MaxAlign = 1U << 31;
    this->Alignment = Alignment.getQuantity() <= MaxAlign
                          ? Alignment.getQuantity()
                          : MaxAlign;
    assert(this->Alignment == Alignment.getQuantity() &&
           "Alignment exceeds allowed max!");
    this->BaseInfo = BaseInfo;
    this->TBAAInfo = TBAAInfo;

    this->Nontemporal = false;
    this->ThreadLocalRef = false;
  }

public:
  bool isSimple() const { return LVType == Simple; }
  bool isVectorElt() const { return LVType == VectorElt; }
  bool isBitField() const { return LVType == BitField; }
  bool isExtVectorElt() const { return LVType == ExtVectorElt; }
  bool isGlobalReg() const { return LVType == GlobalReg; }
  bool isMatrixElt() const { return LVType == MatrixElt; }

  bool isVolatileQualified() const { return Quals.hasVolatile(); }
  bool isRestrictQualified() const { return Quals.hasRestrict(); }
  unsigned getVRQualifiers() const {
    return Quals.getCVRQualifiers() & ~Qualifiers::Const;
  }

  QualType getType() const { return Type; }

  bool isThreadLocalRef() const { return ThreadLocalRef; }
  void setThreadLocalRef(bool Value) { ThreadLocalRef = Value; }

  bool isNontemporal() const { return Nontemporal; }
  void setNontemporal(bool Value) { Nontemporal = Value; }

  bool isVolatile() const { return Quals.hasVolatile(); }

  TBAAAccessInfo getTBAAInfo() const { return TBAAInfo; }
  void setTBAAInfo(TBAAAccessInfo Info) { TBAAInfo = Info; }

  const Qualifiers &getQuals() const { return Quals; }
  Qualifiers &getQuals() { return Quals; }

  LangAS getAddressSpace() const { return Quals.getAddressSpace(); }

  CharUnits getAlignment() const { return CharUnits::fromQuantity(Alignment); }
  void setAlignment(CharUnits A) { Alignment = A.getQuantity(); }

  LValueBaseInfo getBaseInfo() const { return BaseInfo; }
  void setBaseInfo(LValueBaseInfo Info) { BaseInfo = Info; }

  KnownNonNull_t isKnownNonNull() const {
    return (KnownNonNull_t)IsKnownNonNull;
  }
  LValue setKnownNonNull() {
    IsKnownNonNull = true;
    return *this;
  }

  // simple lvalue
  llvm::Value *getPointer(FunctionEmitter &FE) const {
    assert(isSimple());
    return V;
  }
  Address getAddress(FunctionEmitter &FE) const {
    return Address(getPointer(FE), ElementType, getAlignment(),
                   isKnownNonNull());
  }
  void setAddress(Address address) {
    assert(isSimple());
    V = address.getPointer();
    ElementType = address.getElementType();
    Alignment = address.getAlignment().getQuantity();
    IsKnownNonNull = address.isKnownNonNull();
  }

  // vector elt lvalue
  Address getVectorAddress() const {
    return Address(getVectorPointer(), ElementType, getAlignment(),
                   (KnownNonNull_t)isKnownNonNull());
  }
  llvm::Value *getVectorPointer() const {
    assert(isVectorElt());
    return V;
  }
  llvm::Value *getVectorIdx() const {
    assert(isVectorElt());
    return VectorIdx;
  }

  Address getMatrixAddress() const {
    return Address(getMatrixPointer(), ElementType, getAlignment(),
                   (KnownNonNull_t)isKnownNonNull());
  }
  llvm::Value *getMatrixPointer() const {
    assert(isMatrixElt());
    return V;
  }
  llvm::Value *getMatrixIdx() const {
    assert(isMatrixElt());
    return VectorIdx;
  }

  // extended vector elements.
  Address getExtVectorAddress() const {
    return Address(getExtVectorPointer(), ElementType, getAlignment(),
                   (KnownNonNull_t)isKnownNonNull());
  }
  llvm::Value *getExtVectorPointer() const {
    assert(isExtVectorElt());
    return V;
  }
  llvm::Constant *getExtVectorElts() const {
    assert(isExtVectorElt());
    return VectorElts;
  }

  // bitfield lvalue
  Address getBitFieldAddress() const {
    return Address(getBitFieldPointer(), ElementType, getAlignment(),
                   (KnownNonNull_t)isKnownNonNull());
  }
  llvm::Value *getBitFieldPointer() const {
    assert(isBitField());
    return V;
  }
  const BitFieldInfo &getBitFieldInfo() const {
    assert(isBitField());
    return *BitFieldData;
  }

  // global register lvalue
  llvm::Value *getGlobalReg() const {
    assert(isGlobalReg());
    return V;
  }

  static LValue MakeAddr(Address address, QualType type, TreeContext &Context,
                         LValueBaseInfo BaseInfo, TBAAAccessInfo TBAAInfo) {
    Qualifiers qs = type.getQualifiers();

    LValue R;
    R.LVType = Simple;
    assert(address.getPointer()->getType()->isPointerTy());
    R.V = address.getPointer();
    R.ElementType = address.getElementType();
    R.IsKnownNonNull = address.isKnownNonNull();
    R.Initialize(type, qs, address.getAlignment(), BaseInfo, TBAAInfo);
    return R;
  }

  static LValue MakeVectorElt(Address vecAddress, llvm::Value *Idx,
                              QualType type, LValueBaseInfo BaseInfo,
                              TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = VectorElt;
    R.V = vecAddress.getPointer();
    R.ElementType = vecAddress.getElementType();
    R.VectorIdx = Idx;
    R.IsKnownNonNull = vecAddress.isKnownNonNull();
    R.Initialize(type, type.getQualifiers(), vecAddress.getAlignment(),
                 BaseInfo, TBAAInfo);
    return R;
  }

  static LValue MakeExtVectorElt(Address vecAddress, llvm::Constant *Elts,
                                 QualType type, LValueBaseInfo BaseInfo,
                                 TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = ExtVectorElt;
    R.V = vecAddress.getPointer();
    R.ElementType = vecAddress.getElementType();
    R.VectorElts = Elts;
    R.IsKnownNonNull = vecAddress.isKnownNonNull();
    R.Initialize(type, type.getQualifiers(), vecAddress.getAlignment(),
                 BaseInfo, TBAAInfo);
    return R;
  }

  static LValue MakeBitfield(Address Addr, const BitFieldInfo &Info,
                             QualType type, LValueBaseInfo BaseInfo,
                             TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = BitField;
    R.V = Addr.getPointer();
    R.ElementType = Addr.getElementType();
    R.BitFieldData = &Info;
    R.IsKnownNonNull = Addr.isKnownNonNull();
    R.Initialize(type, type.getQualifiers(), Addr.getAlignment(), BaseInfo,
                 TBAAInfo);
    return R;
  }

  static LValue MakeGlobalReg(llvm::Value *V, CharUnits alignment,
                              QualType type) {
    LValue R;
    R.LVType = GlobalReg;
    R.V = V;
    R.ElementType = nullptr;
    R.IsKnownNonNull = true;
    R.Initialize(type, type.getQualifiers(), alignment,
                 LValueBaseInfo(AlignmentSource::Decl), TBAAAccessInfo());
    return R;
  }

  static LValue MakeMatrixElt(Address matAddress, llvm::Value *Idx,
                              QualType type, LValueBaseInfo BaseInfo,
                              TBAAAccessInfo TBAAInfo) {
    LValue R;
    R.LVType = MatrixElt;
    R.V = matAddress.getPointer();
    R.ElementType = matAddress.getElementType();
    R.VectorIdx = Idx;
    R.IsKnownNonNull = matAddress.isKnownNonNull();
    R.Initialize(type, type.getQualifiers(), matAddress.getAlignment(),
                 BaseInfo, TBAAInfo);
    return R;
  }

  RValue asAggregateRValue(FunctionEmitter &FE) const {
    return RValue::getAggregate(getAddress(FE), isVolatileQualified());
  }
};

class AggValueSlot {
  Address Addr;

  // Qualifiers
  Qualifiers Quals;

  bool DestructedFlag : 1;

  bool ZeroedFlag : 1;

  bool AliasedFlag : 1;

  bool OverlapFlag : 1;

  bool SanitizerCheckedFlag : 1;

  AggValueSlot(Address Addr, Qualifiers Quals, bool DestructedFlag,
               bool ZeroedFlag, bool AliasedFlag, bool OverlapFlag,
               bool SanitizerCheckedFlag)
      : Addr(Addr), Quals(Quals), DestructedFlag(DestructedFlag),
        ZeroedFlag(ZeroedFlag), AliasedFlag(AliasedFlag),
        OverlapFlag(OverlapFlag), SanitizerCheckedFlag(SanitizerCheckedFlag) {}

public:
  enum IsAliased_t { IsNotAliased, IsAliased };
  enum IsDestructed_t { IsNotDestructed, IsDestructed };
  enum IsZeroed_t { IsNotZeroed, IsZeroed };
  enum Overlap_t { DoesNotOverlap, MayOverlap };
  enum IsSanitizerChecked_t { IsNotSanitizerChecked, IsSanitizerChecked };

  static AggValueSlot ignored() {
    return forAddr(Address::invalid(), Qualifiers(), IsNotDestructed,
                   IsNotAliased, DoesNotOverlap);
  }

  static AggValueSlot
  forAddr(Address addr, Qualifiers quals, IsDestructed_t isDestructed,
          IsAliased_t isAliased, Overlap_t mayOverlap,
          IsZeroed_t isZeroed = IsNotZeroed,
          IsSanitizerChecked_t isChecked = IsNotSanitizerChecked) {
    if (addr.isValid())
      addr.setKnownNonNull();
    return AggValueSlot(addr, quals, isDestructed, isZeroed, isAliased,
                        mayOverlap, isChecked);
  }

  static AggValueSlot
  forLValue(const LValue &LV, FunctionEmitter &FE, IsDestructed_t isDestructed,
            IsAliased_t isAliased, Overlap_t mayOverlap,
            IsZeroed_t isZeroed = IsNotZeroed,
            IsSanitizerChecked_t isChecked = IsNotSanitizerChecked) {
    return forAddr(LV.getAddress(FE), LV.getQuals(), isDestructed, isAliased,
                   mayOverlap, isZeroed, isChecked);
  }

  IsDestructed_t isExternallyDestructed() const {
    return IsDestructed_t(DestructedFlag);
  }
  void setExternallyDestructed(bool destructed = true) {
    DestructedFlag = destructed;
  }

  Qualifiers getQualifiers() const { return Quals; }

  bool isVolatile() const { return Quals.hasVolatile(); }

  void setVolatile(bool flag) {
    if (flag)
      Quals.addVolatile();
    else
      Quals.removeVolatile();
  }

  llvm::Value *getPointer() const { return Addr.getPointer(); }

  Address getAddress() const { return Addr; }

  bool isIgnored() const { return !Addr.isValid(); }

  CharUnits getAlignment() const { return Addr.getAlignment(); }

  IsAliased_t isPotentiallyAliased() const { return IsAliased_t(AliasedFlag); }

  Overlap_t mayOverlap() const { return Overlap_t(OverlapFlag); }

  bool isSanitizerChecked() const { return SanitizerCheckedFlag; }

  RValue asRValue() const {
    if (isIgnored()) {
      return RValue::getIgnored();
    } else {
      return RValue::getAggregate(getAddress(), isVolatile());
    }
  }

  void setZeroed(bool V = true) { ZeroedFlag = V; }
  IsZeroed_t isZeroed() const { return IsZeroed_t(ZeroedFlag); }

  CharUnits getPreferredSize(TreeContext &Ctx, QualType Type) const {
    return mayOverlap() ? Ctx.getTypeInfoDataSizeInChars(Type).Width
                        : Ctx.getTypeSizeInChars(Type);
  }
};

} // end namespace Emit
} // end namespace neverc

#endif
