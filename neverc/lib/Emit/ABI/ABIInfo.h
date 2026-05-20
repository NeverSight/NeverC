#ifndef NEVERC_LIB_EMIT_ABI_ABIINFO_H
#define NEVERC_LIB_EMIT_ABI_ABIINFO_H

#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Type.h"

namespace llvm {
class LLVMContext;
class DataLayout;
class Type;
} // namespace llvm

namespace neverc {
class TreeContext;
class CodeGenOptions;
class TargetInfo;

namespace Emit {
class ABIArgInfo;
class Address;
class CGABI;
class ABIFunctionInfo;
class FunctionEmitter;
class TypeEmitter;

class ABIInfo {
protected:
  Emit::TypeEmitter &CGT;
  llvm::CallingConv::ID RuntimeCC;

public:
  ABIInfo(Emit::TypeEmitter &cgt) : CGT(cgt), RuntimeCC(llvm::CallingConv::C) {}

  virtual ~ABIInfo();

  virtual bool allowBFloatArgsAndRet() const { return false; }

  Emit::CGABI &getCGABI() const;
  TreeContext &getContext() const;
  llvm::LLVMContext &getVMContext() const;
  const llvm::DataLayout &getDataLayout() const;
  const TargetInfo &getTarget() const;
  const CodeGenOptions &getCodeGenOpts() const;

  llvm::CallingConv::ID getRuntimeCC() const { return RuntimeCC; }

  virtual void computeInfo(Emit::ABIFunctionInfo &FI) const = 0;

  virtual Emit::Address genVAArg(Emit::FunctionEmitter &FE,
                                 Emit::Address VAListAddr,
                                 QualType Ty) const = 0;

  bool isAndroid() const;

  virtual Emit::Address genMSVAArg(Emit::FunctionEmitter &FE,
                                   Emit::Address VAListAddr, QualType Ty) const;

  virtual bool isHomogeneousAggregateBaseType(QualType Ty) const;

  virtual bool isHomogeneousAggregateSmallEnough(const Type *Base,
                                                 uint64_t Members) const;
  virtual bool isZeroLengthBitfieldPermittedInHomogeneousAggregate() const;

  bool isHomogeneousAggregate(QualType Ty, const Type *&Base,
                              uint64_t &Members) const;

  // Implement the Type::IsPromotableIntegerType for ABI specific needs. The
  // only difference is that this considers bit-precise integer types as well.
  bool isPromotableIntegerTypeForABI(QualType Ty) const;

  Emit::ABIArgInfo getNaturalAlignIndirect(QualType Ty, bool ByVal = true,
                                           bool Realign = false,
                                           llvm::Type *Padding = nullptr) const;
};

} // end namespace Emit
} // end namespace neverc

#endif
