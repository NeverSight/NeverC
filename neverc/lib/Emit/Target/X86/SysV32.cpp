//===- SysV32.cpp - i386 System V C ABI -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Adapted from Clang's X86_32ABIInfo for NeverC's C23 type/emitter interfaces.
// Record arguments use byval rather than Clang's optional scalar expansion;
// both describe the same stack image. Records return through a hidden pointer.
//===----------------------------------------------------------------------===//

#include "ABI/ABIInfoImpl.h"
#include "ABI/TargetInfo.h"

using namespace neverc;
using namespace neverc::Emit;
using namespace neverc::CodeGen;

namespace {
class X86_32ABIInfo final : public ABIInfo {
  static constexpr unsigned RegisterBits = 32;
  static constexpr unsigned StackSlotBytes = 4;

  unsigned stackAlign(QualType Ty) const {
    unsigned Align = getContext().getTypeAlign(Ty) / 8;
    if (Ty->isVectorType() && (Align == 16 || Align == 32 || Align == 64))
      return Align;
    return StackSlotBytes;
  }

  bool consumeRegisters(QualType Ty, unsigned &FreeRegs) const {
    const Type *Single = isSingleElementStruct(Ty, getContext());
    const Type *Base = Single ? Single : Ty.getTypePtr();
    if (Base->isSpecificBuiltinType(BuiltinType::Float) ||
        Base->isSpecificBuiltinType(BuiltinType::Double))
      return false;
    unsigned Count =
        (getContext().getTypeSize(Ty) + RegisterBits - 1) / RegisterBits;
    if (!Count)
      return false;
    if (Count > FreeRegs) {
      FreeRegs = 0;
      return false;
    }
    FreeRegs -= Count;
    return true;
  }

  ABIArgInfo indirect(QualType Ty, unsigned &FreeRegs) const {
    ABIArgInfo AI = getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    if (FreeRegs) {
      --FreeRegs;
      AI.setInReg(true);
    }
    return AI;
  }

  ABIArgInfo classifyReturn(QualType Ty, unsigned &FreeRegs) const {
    if (Ty->isVoidType())
      return ABIArgInfo::getIgnore();
    if (const auto *CT = Ty->getAs<ComplexType>()) {
      unsigned Bits = getContext().getTypeSize(Ty);
      if (CT->getElementType()->isFloat16Type())
        return ABIArgInfo::getDirect(llvm::FixedVectorType::get(
            llvm::Type::getHalfTy(getVMContext()), 2));
      if (Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64)
        return ABIArgInfo::getDirect(
            llvm::IntegerType::get(getVMContext(), Bits));
      return indirect(Ty, FreeRegs);
    }
    if (isAggregateTypeForABI(Ty))
      return indirect(Ty, FreeRegs);
    if (const auto *ET = Ty->getAs<EnumType>())
      Ty = ET->getDecl()->getIntegerType();
    if (const auto *BT = Ty->getAs<BitIntType>(); BT && BT->getNumBits() > 64)
      return indirect(Ty, FreeRegs);
    return isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                             : ABIArgInfo::getDirect();
  }

  ABIArgInfo classifyArgument(QualType Ty, unsigned &FreeRegs) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);
    if (isAggregateTypeForABI(Ty)) {
      if (isEmptyRecord(getContext(), Ty, true) ||
          !getContext().getTypeSize(Ty))
        return ABIArgInfo::getIgnore();
      const auto *RT = Ty->getAs<RecordType>();
      const bool Flexible = RT && RT->getDecl()->hasFlexibleArrayMember();
      if (!Flexible && consumeRegisters(Ty, FreeRegs)) {
        unsigned Count =
            (getContext().getTypeSize(Ty) + RegisterBits - 1) / RegisterBits;
        llvm::SmallVector<llvm::Type *, 3> Fields(
            Count, llvm::Type::getInt32Ty(getVMContext()));
        ABIArgInfo AI = ABIArgInfo::getDirect(
            llvm::StructType::get(getVMContext(), Fields));
        AI.setInReg(true);
        return AI;
      }
      unsigned Align = stackAlign(Ty);
      return ABIArgInfo::getIndirect(CharUnits::fromQuantity(Align),
                                     /*ByVal=*/true,
                                     getContext().getTypeAlign(Ty) / 8 > Align);
    }
    if (const auto *VT = Ty->getAs<VectorType>()) {
      if (getContext().getTypeSize(Ty) == 64 &&
          VT->getElementType()->isIntegerType() &&
          getContext().getTypeSize(VT->getElementType()) != 64)
        return ABIArgInfo::getDirect(llvm::Type::getInt64Ty(getVMContext()));
      return ABIArgInfo::getDirect();
    }
    if (const auto *ET = Ty->getAs<EnumType>())
      Ty = ET->getDecl()->getIntegerType();
    // Match the register-parameter ABI before deciding whether a wide scalar
    // needs an indirect temporary. Its full size can exhaust the register bank.
    bool InReg = consumeRegisters(Ty, FreeRegs);
    if (const auto *BT = Ty->getAs<BitIntType>(); BT && BT->getNumBits() > 64)
      return indirect(Ty, FreeRegs);
    ABIArgInfo AI = isPromotableIntegerTypeForABI(Ty)
                        ? ABIArgInfo::getExtend(Ty)
                        : ABIArgInfo::getDirect();
    AI.setInReg(InReg);
    return AI;
  }

public:
  explicit X86_32ABIInfo(TypeEmitter &Types) : ABIInfo(Types) {}

  void computeInfo(ABIFunctionInfo &FI) const override {
    unsigned FreeRegs =
        !FI.isVariadic() && FI.getHasRegParm() ? FI.getRegParm() : 0;
    FI.getReturnInfo() = classifyReturn(FI.getReturnType(), FreeRegs);
    for (auto &Arg : FI.arguments())
      Arg.info = classifyArgument(Arg.type, FreeRegs);
  }

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Ty) const override {
    unsigned FreeRegs = 0;
    ABIArgInfo AI = classifyArgument(Ty, FreeRegs);
    const bool Indirect = AI.isIndirect() && !AI.getIndirectByVal();
    auto Info = getContext().getTypeInfoInChars(Ty);
    if (!Indirect)
      Info.Align = CharUnits::fromQuantity(stackAlign(Ty));
    return emitVoidPtrVAArg(FE, VAListAddr, Ty, Indirect, Info,
                            CharUnits::fromQuantity(StackSlotBytes), true);
  }
};

class X86_32TargetCodeGenInfo final : public TargetCodeGenInfo {
public:
  explicit X86_32TargetCodeGenInfo(TypeEmitter &Types)
      : TargetCodeGenInfo(std::make_unique<X86_32ABIInfo>(Types)) {}
  int getDwarfEHStackPointer(ModuleEmitter &) const override { return 4; }
};
} // namespace

std::unique_ptr<TargetCodeGenInfo>
neverc::Emit::createX86_32TargetCodeGenInfo(ModuleEmitter &ME) {
  return std::make_unique<X86_32TargetCodeGenInfo>(ME.getTypes());
}
