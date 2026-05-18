#ifndef NEVERC_CODEGEN_ABI_ABIFUNCTIONINFO_H
#define NEVERC_CODEGEN_ABI_ABIFUNCTIONINFO_H

#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Type/CanonicalType.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/TrailingObjects.h"
#include <cassert>

namespace neverc {
namespace Emit {

class ABIArgInfo {
public:
  enum Kind : uint8_t {
    /// Direct - Pass the argument directly using the normal converted LLVM
    /// type, or by coercing to another specified type stored in
    /// 'CoerceToType').  If an offset is specified (in UIntData), then the
    /// argument passed is offset by some number of bytes in the memory
    /// representation. A dummy argument is emitted before the real argument
    /// if the specified type stored in "PaddingType" is not zero.
    Direct,

    /// Extend - Valid only for integer argument types. Same as 'direct'
    /// but also emit a zero/sign extension attribute.
    Extend,

    /// Indirect - Pass the argument indirectly via a hidden pointer with the
    /// specified alignment (0 indicates default alignment) and address space.
    Indirect,

    /// IndirectAliased - Similar to Indirect, but the pointer may be to an
    /// object that is otherwise referenced.  The object is known to not be
    /// modified through any other references for the duration of the call, and
    /// the callee must not itself modify the object.  Because C allows
    /// parameter variables to be modified and guarantees that they have unique
    /// addresses, the callee must defensively copy the object into a local
    /// variable if it might be modified or its address might be compared.
    /// Since those are uncommon, in principle this convention allows programs
    /// to avoid copies in more situations.  However, it may introduce *extra*
    /// copies if the callee fails to prove that a copy is unnecessary and the
    /// caller naturally produces an unaliased object for the argument.
    IndirectAliased,

    /// Ignore - Ignore the argument (treat as void). Useful for void and
    /// empty structs.
    Ignore,

    /// Expand - Only valid for aggregate argument types. The structure should
    /// be expanded into consecutive arguments for its constituent fields.
    /// Currently expand is only allowed on structures whose fields
    /// are all scalar types or are themselves expandable types.
    Expand,

    /// CoerceAndExpand - Only valid for aggregate argument types. The
    /// structure should be expanded into consecutive arguments corresponding
    /// to the non-array elements of the type stored in CoerceToType.
    /// Array elements in the type are assumed to be padding and skipped.
    CoerceAndExpand,
    KindFirst = Direct,
    KindLast = CoerceAndExpand
  };

private:
  llvm::Type *TypeData; // canHaveCoerceToType()
  union {
    llvm::Type *PaddingType;                 // canHavePaddingType()
    llvm::Type *UnpaddedCoerceAndExpandType; // isCoerceAndExpand()
  };
  struct DirectAttrInfo {
    unsigned Offset;
    unsigned Align;
  };
  struct IndirectAttrInfo {
    unsigned Align;
    unsigned AddrSpace;
  };
  union {
    DirectAttrInfo DirectAttr;     // isDirect() || isExtend()
    IndirectAttrInfo IndirectAttr; // isIndirect()
  };
  Kind TheKind;
  bool PaddingInReg : 1;
  bool IndirectByVal : 1;   // isIndirect()
  bool IndirectRealign : 1; // isIndirect()
  bool SRetAfterThis : 1;   // isIndirect()
  bool InReg : 1;           // isDirect() || isExtend() || isIndirect()
  bool CanBeFlattened : 1;  // isDirect()
  bool SignExt : 1;         // isExtend()

  bool canHavePaddingType() const {
    return isDirect() || isExtend() || isIndirect() || isIndirectAliased() ||
           isExpand();
  }
  void setPaddingType(llvm::Type *T) {
    assert(canHavePaddingType());
    PaddingType = T;
  }

public:
  ABIArgInfo(Kind K = Direct)
      : TypeData(nullptr), PaddingType(nullptr), DirectAttr{0, 0}, TheKind(K),
        PaddingInReg(false), IndirectByVal(false), IndirectRealign(false),
        SRetAfterThis(false), InReg(false), CanBeFlattened(false),
        SignExt(false) {}

  static ABIArgInfo getDirect(llvm::Type *T = nullptr, unsigned Offset = 0,
                              llvm::Type *Padding = nullptr,
                              bool CanBeFlattened = true, unsigned Align = 0) {
    auto AI = ABIArgInfo(Direct);
    AI.setCoerceToType(T);
    AI.setPaddingType(Padding);
    AI.setDirectOffset(Offset);
    AI.setDirectAlign(Align);
    AI.setCanBeFlattened(CanBeFlattened);
    return AI;
  }

  // ABIArgInfo will record the argument as being extended based on the sign
  // of its type.
  static ABIArgInfo getExtend(QualType Ty, llvm::Type *T = nullptr) {
    assert(Ty->isIntegralOrEnumerationType() && "Unexpected QualType");
    auto AI = ABIArgInfo(Extend);
    AI.setCoerceToType(T);
    AI.setPaddingType(nullptr);
    AI.setDirectOffset(0);
    AI.setDirectAlign(0);
    AI.setSignExt(Ty->hasSignedIntegerRepresentation());
    return AI;
  }

  static ABIArgInfo getIgnore() { return ABIArgInfo(Ignore); }
  static ABIArgInfo getIndirect(CharUnits Alignment, bool ByVal = true,
                                bool Realign = false,
                                llvm::Type *Padding = nullptr) {
    auto AI = ABIArgInfo(Indirect);
    AI.setIndirectAlign(Alignment);
    AI.setIndirectByVal(ByVal);
    AI.setIndirectRealign(Realign);
    AI.setSRetAfterThis(false);
    AI.setPaddingType(Padding);
    return AI;
  }

  static ABIArgInfo getExpand() {
    auto AI = ABIArgInfo(Expand);
    AI.setPaddingType(nullptr);
    return AI;
  }

  static bool isPaddingForCoerceAndExpand(llvm::Type *eltType) {
    if (eltType->isArrayTy()) {
      assert(eltType->getArrayElementType()->isIntegerTy(8));
      return true;
    } else {
      return false;
    }
  }

  Kind getKind() const { return TheKind; }
  bool isDirect() const { return TheKind == Direct; }
  bool isExtend() const { return TheKind == Extend; }
  bool isIgnore() const { return TheKind == Ignore; }
  bool isIndirect() const { return TheKind == Indirect; }
  bool isIndirectAliased() const { return TheKind == IndirectAliased; }
  bool isExpand() const { return TheKind == Expand; }
  bool isCoerceAndExpand() const { return TheKind == CoerceAndExpand; }

  bool canHaveCoerceToType() const {
    return isDirect() || isExtend() || isCoerceAndExpand();
  }

  // Direct/Extend accessors
  unsigned getDirectOffset() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return DirectAttr.Offset;
  }
  void setDirectOffset(unsigned Offset) {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    DirectAttr.Offset = Offset;
  }

  unsigned getDirectAlign() const {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    return DirectAttr.Align;
  }
  void setDirectAlign(unsigned Align) {
    assert((isDirect() || isExtend()) && "Not a direct or extend kind");
    DirectAttr.Align = Align;
  }

  bool isSignExt() const {
    assert(isExtend() && "Invalid kind!");
    return SignExt;
  }
  void setSignExt(bool SExt) {
    assert(isExtend() && "Invalid kind!");
    SignExt = SExt;
  }

  llvm::Type *getPaddingType() const {
    return (canHavePaddingType() ? PaddingType : nullptr);
  }

  bool getPaddingInReg() const { return PaddingInReg; }
  void setPaddingInReg(bool PIR) { PaddingInReg = PIR; }

  llvm::Type *getCoerceToType() const {
    assert(canHaveCoerceToType() && "Invalid kind!");
    return TypeData;
  }

  void setCoerceToType(llvm::Type *T) {
    assert(canHaveCoerceToType() && "Invalid kind!");
    TypeData = T;
  }

  llvm::StructType *getCoerceAndExpandType() const {
    assert(isCoerceAndExpand());
    return cast<llvm::StructType>(TypeData);
  }

  llvm::Type *getUnpaddedCoerceAndExpandType() const {
    assert(isCoerceAndExpand());
    return UnpaddedCoerceAndExpandType;
  }

  llvm::ArrayRef<llvm::Type *> getCoerceAndExpandTypeSequence() const {
    assert(isCoerceAndExpand());
    if (auto structTy =
            dyn_cast<llvm::StructType>(UnpaddedCoerceAndExpandType)) {
      return structTy->elements();
    } else {
      return llvm::ArrayRef(&UnpaddedCoerceAndExpandType, 1);
    }
  }

  bool getInReg() const {
    assert((isDirect() || isExtend() || isIndirect()) && "Invalid kind!");
    return InReg;
  }

  void setInReg(bool IR) {
    assert((isDirect() || isExtend() || isIndirect()) && "Invalid kind!");
    InReg = IR;
  }

  // Indirect accessors
  CharUnits getIndirectAlign() const {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    return CharUnits::fromQuantity(IndirectAttr.Align);
  }
  void setIndirectAlign(CharUnits IA) {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    IndirectAttr.Align = IA.getQuantity();
  }

  bool getIndirectByVal() const {
    assert(isIndirect() && "Invalid kind!");
    return IndirectByVal;
  }
  void setIndirectByVal(bool IBV) {
    assert(isIndirect() && "Invalid kind!");
    IndirectByVal = IBV;
  }

  unsigned getIndirectAddrSpace() const {
    assert(isIndirectAliased() && "Invalid kind!");
    return IndirectAttr.AddrSpace;
  }

  bool getIndirectRealign() const {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    return IndirectRealign;
  }
  void setIndirectRealign(bool IR) {
    assert((isIndirect() || isIndirectAliased()) && "Invalid kind!");
    IndirectRealign = IR;
  }

  bool isSRetAfterThis() const {
    assert(isIndirect() && "Invalid kind!");
    return SRetAfterThis;
  }
  void setSRetAfterThis(bool AfterThis) {
    assert(isIndirect() && "Invalid kind!");
    SRetAfterThis = AfterThis;
  }

  bool getCanBeFlattened() const {
    assert(isDirect() && "Invalid kind!");
    return CanBeFlattened;
  }

  void setCanBeFlattened(bool Flatten) {
    assert(isDirect() && "Invalid kind!");
    CanBeFlattened = Flatten;
  }

  void dump() const;
};

class RequiredArgs {
  unsigned NumRequired;

public:
  enum All_t { All };

  RequiredArgs(All_t _) : NumRequired(~0U) {}
  explicit RequiredArgs(unsigned n) : NumRequired(n) { assert(n != ~0U); }

  static RequiredArgs forPrototypePlus(const FunctionProtoType *prototype,
                                       unsigned additional) {
    if (!prototype->isVariadic())
      return All;

    if (prototype->hasExtParameterInfos())
      additional += llvm::count_if(
          prototype->getExtParameterInfos(),
          [](const FunctionProtoType::ExtParameterInfo &ExtInfo) {
            return ExtInfo.hasPassObjectSize();
          });

    return RequiredArgs(prototype->getNumParams() + additional);
  }

  static RequiredArgs forPrototypePlus(CanQual<FunctionProtoType> prototype,
                                       unsigned additional) {
    return forPrototypePlus(prototype.getTypePtr(), additional);
  }

  static RequiredArgs forPrototype(const FunctionProtoType *prototype) {
    return forPrototypePlus(prototype, 0);
  }

  static RequiredArgs forPrototype(CanQual<FunctionProtoType> prototype) {
    return forPrototypePlus(prototype.getTypePtr(), 0);
  }

  bool allowsOptionalArgs() const { return NumRequired != ~0U; }
  unsigned getNumRequiredArgs() const {
    assert(allowsOptionalArgs());
    return NumRequired;
  }

  bool isRequiredArg(unsigned argIdx) const {
    return argIdx == ~0U || argIdx < NumRequired;
  }

  unsigned getOpaqueData() const { return NumRequired; }
  static RequiredArgs getFromOpaqueData(unsigned value) {
    if (value == ~0U)
      return All;
    return RequiredArgs(value);
  }
};

// Implementation detail of ABIFunctionInfo, factored out so it can be named
// in the TrailingObjects base class of ABIFunctionInfo.
struct ABIFunctionInfoArgInfo {
  CanQualType type;
  ABIArgInfo info;
};

class ABIFunctionInfo final
    : public llvm::FoldingSetNode,
      private llvm::TrailingObjects<ABIFunctionInfo, ABIFunctionInfoArgInfo,
                                    FunctionProtoType::ExtParameterInfo> {
  typedef ABIFunctionInfoArgInfo ArgInfo;
  typedef FunctionProtoType::ExtParameterInfo ExtParameterInfo;

  unsigned CallingConvention : 8;

  unsigned EffectiveCallingConvention : 8;

  unsigned ASTCallingConvention : 6;

  unsigned InstanceMethod : 1;

  unsigned ChainCall : 1;

  unsigned DelegateCall : 1;

  unsigned CmseNSCall : 1;

  unsigned NoReturn : 1;

  unsigned ReturnsRetained : 1;

  unsigned NoCallerSavedRegs : 1;

  unsigned HasRegParm : 1;
  unsigned RegParm : 3;

  unsigned NoCfCheck : 1;

  unsigned MaxVectorWidth : 4;

  RequiredArgs Required;

  unsigned HasExtParameterInfos : 1;

  unsigned NumArgs;

  ArgInfo *getArgsBuffer() { return getTrailingObjects<ArgInfo>(); }
  const ArgInfo *getArgsBuffer() const { return getTrailingObjects<ArgInfo>(); }

  ExtParameterInfo *getExtParameterInfosBuffer() {
    return getTrailingObjects<ExtParameterInfo>();
  }
  const ExtParameterInfo *getExtParameterInfosBuffer() const {
    return getTrailingObjects<ExtParameterInfo>();
  }

  ABIFunctionInfo() : Required(RequiredArgs::All) {}

public:
  static ABIFunctionInfo *
  create(unsigned llvmCC, bool instanceMethod, bool chainCall,
         bool delegateCall, const FunctionType::ExtInfo &extInfo,
         llvm::ArrayRef<ExtParameterInfo> paramInfos, CanQualType resultType,
         llvm::ArrayRef<CanQualType> argTypes, RequiredArgs required);
  void operator delete(void *p) { ::operator delete(p); }

  // Friending class TrailingObjects is apparently not good enough for MSVC,
  // so these have to be public.
  friend class TrailingObjects;
  size_t numTrailingObjects(OverloadToken<ArgInfo>) const {
    return NumArgs + 1;
  }
  size_t numTrailingObjects(OverloadToken<ExtParameterInfo>) const {
    return (HasExtParameterInfos ? NumArgs : 0);
  }

  typedef const ArgInfo *const_arg_iterator;
  typedef ArgInfo *arg_iterator;

  llvm::MutableArrayRef<ArgInfo> arguments() {
    return llvm::MutableArrayRef<ArgInfo>(arg_begin(), NumArgs);
  }
  llvm::ArrayRef<ArgInfo> arguments() const {
    return llvm::ArrayRef<ArgInfo>(arg_begin(), NumArgs);
  }

  const_arg_iterator arg_begin() const { return getArgsBuffer() + 1; }
  const_arg_iterator arg_end() const { return getArgsBuffer() + 1 + NumArgs; }
  arg_iterator arg_begin() { return getArgsBuffer() + 1; }
  arg_iterator arg_end() { return getArgsBuffer() + 1 + NumArgs; }

  unsigned arg_size() const { return NumArgs; }

  bool isVariadic() const { return Required.allowsOptionalArgs(); }
  RequiredArgs getRequiredArgs() const { return Required; }
  unsigned getNumRequiredArgs() const {
    return isVariadic() ? getRequiredArgs().getNumRequiredArgs() : arg_size();
  }

  bool isInstanceMethod() const { return InstanceMethod; }

  bool isChainCall() const { return ChainCall; }

  bool isDelegateCall() const { return DelegateCall; }

  bool isCmseNSCall() const { return CmseNSCall; }

  bool isNoReturn() const { return NoReturn; }

  bool isReturnsRetained() const { return ReturnsRetained; }

  bool isNoCallerSavedRegs() const { return NoCallerSavedRegs; }

  bool isNoCfCheck() const { return NoCfCheck; }

  CallingConv getASTCallingConvention() const {
    return CallingConv(ASTCallingConvention);
  }

  unsigned getCallingConvention() const { return CallingConvention; }

  unsigned getEffectiveCallingConvention() const {
    return EffectiveCallingConvention;
  }
  void setEffectiveCallingConvention(unsigned Value) {
    EffectiveCallingConvention = Value;
  }

  bool getHasRegParm() const { return HasRegParm; }
  unsigned getRegParm() const { return RegParm; }

  FunctionType::ExtInfo getExtInfo() const {
    return FunctionType::ExtInfo(isNoReturn(), getHasRegParm(), getRegParm(),
                                 getASTCallingConvention(), isReturnsRetained(),
                                 isNoCallerSavedRegs(), isNoCfCheck(),
                                 isCmseNSCall());
  }

  CanQualType getReturnType() const { return getArgsBuffer()[0].type; }

  ABIArgInfo &getReturnInfo() { return getArgsBuffer()[0].info; }
  const ABIArgInfo &getReturnInfo() const { return getArgsBuffer()[0].info; }

  llvm::ArrayRef<ExtParameterInfo> getExtParameterInfos() const {
    if (!HasExtParameterInfos)
      return {};
    return llvm::ArrayRef(getExtParameterInfosBuffer(), NumArgs);
  }
  ExtParameterInfo getExtParameterInfo(unsigned argIndex) const {
    assert(argIndex <= NumArgs);
    if (!HasExtParameterInfos)
      return ExtParameterInfo();
    return getExtParameterInfos()[argIndex];
  }

  unsigned getMaxVectorWidth() const {
    return MaxVectorWidth ? 1U << (MaxVectorWidth - 1) : 0;
  }

  void setMaxVectorWidth(unsigned Width) {
    assert(llvm::isPowerOf2_32(Width) && "Expected power of 2 vector");
    MaxVectorWidth = llvm::countr_zero(Width) + 1;
  }

  void Profile(llvm::FoldingSetNodeID &ID) {
    ID.AddInteger(getASTCallingConvention());
    ID.AddBoolean(InstanceMethod);
    ID.AddBoolean(ChainCall);
    ID.AddBoolean(DelegateCall);
    ID.AddBoolean(NoReturn);
    ID.AddBoolean(ReturnsRetained);
    ID.AddBoolean(NoCallerSavedRegs);
    ID.AddBoolean(HasRegParm);
    ID.AddInteger(RegParm);
    ID.AddBoolean(NoCfCheck);
    ID.AddBoolean(CmseNSCall);
    ID.AddInteger(Required.getOpaqueData());
    ID.AddBoolean(HasExtParameterInfos);
    if (HasExtParameterInfos) {
      for (auto paramInfo : getExtParameterInfos())
        ID.AddInteger(paramInfo.getOpaqueValue());
    }
    getReturnType().Profile(ID);
    for (const auto &I : arguments())
      I.type.Profile(ID);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, bool InstanceMethod,
                      bool ChainCall, bool IsDelegateCall,
                      const FunctionType::ExtInfo &info,
                      llvm::ArrayRef<ExtParameterInfo> paramInfos,
                      RequiredArgs required, CanQualType resultType,
                      llvm::ArrayRef<CanQualType> argTypes) {
    ID.AddInteger(info.getCC());
    ID.AddBoolean(InstanceMethod);
    ID.AddBoolean(ChainCall);
    ID.AddBoolean(IsDelegateCall);
    ID.AddBoolean(info.getNoReturn());
    ID.AddBoolean(info.getProducesResult());
    ID.AddBoolean(info.getNoCallerSavedRegs());
    ID.AddBoolean(info.getHasRegParm());
    ID.AddInteger(info.getRegParm());
    ID.AddBoolean(info.getNoCfCheck());
    ID.AddBoolean(info.getCmseNSCall());
    ID.AddInteger(required.getOpaqueData());
    ID.AddBoolean(!paramInfos.empty());
    if (!paramInfos.empty()) {
      for (auto paramInfo : paramInfos)
        ID.AddInteger(paramInfo.getOpaqueValue());
    }
    resultType.Profile(ID);
    for (llvm::ArrayRef<CanQualType>::iterator i = argTypes.begin(),
                                               e = argTypes.end();
         i != e; ++i) {
      i->Profile(ID);
    }
  }
};

} // end namespace Emit
} // end namespace neverc

#endif
