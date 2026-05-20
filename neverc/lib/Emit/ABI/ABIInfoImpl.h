#ifndef NEVERC_LIB_EMIT_ABI_ABIINFOIMPL_H
#define NEVERC_LIB_EMIT_ABI_ABIINFOIMPL_H

#include "ABI/ABIInfo.h"
#include "ABI/EmitterABI.h"

namespace neverc::CodeGen {
using namespace Emit;

class DefaultABIInfo : public ABIInfo {
public:
  DefaultABIInfo(Emit::TypeEmitter &CGT) : ABIInfo(CGT) {}

  virtual ~DefaultABIInfo();

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;

  void computeInfo(ABIFunctionInfo &FI) const override;

  Address genVAArg(FunctionEmitter &FE, Address VAListAddr,
                   QualType Ty) const override;
};

void assignToArrayRange(Emit::CGBuilderTy &Builder, llvm::Value *Array,
                        llvm::Value *Value, unsigned FirstIndex,
                        unsigned LastIndex);

bool isAggregateTypeForABI(QualType T);

CGABI::RecordArgABI getRecordArgABI(const RecordType *RT);

CGABI::RecordArgABI getRecordArgABI(QualType T);

bool classifyReturnType(ABIFunctionInfo &FI, const ABIInfo &Info);

QualType useFirstFieldIfTransparentUnion(QualType Ty);

// Dynamically round a pointer up to a multiple of the given alignment.
llvm::Value *emitRoundPointerUpToAlignment(FunctionEmitter &FE,
                                           llvm::Value *Ptr, CharUnits Align);

Address emitVoidPtrDirectVAArg(FunctionEmitter &FE, Address VAListAddr,
                               llvm::Type *DirectTy, CharUnits DirectSize,
                               CharUnits DirectAlign, CharUnits SlotSize,
                               bool AllowHigherAlign);

Address emitVoidPtrVAArg(FunctionEmitter &FE, Address VAListAddr,
                         QualType ValueTy, bool IsIndirect,
                         TypeInfoChars ValueInfo, CharUnits SlotSizeAndAlign,
                         bool AllowHigherAlign);

Address emitMergePHI(FunctionEmitter &FE, Address Addr1,
                     llvm::BasicBlock *Block1, Address Addr2,
                     llvm::BasicBlock *Block2, const llvm::Twine &Name = "");

bool isEmptyField(TreeContext &Context, const FieldDecl *FD, bool AllowArrays);

bool isEmptyRecord(TreeContext &Context, QualType T, bool AllowArrays);

const Type *isSingleElementStruct(QualType T, TreeContext &Context);

Address genVAArgInstr(FunctionEmitter &FE, Address VAListAddr, QualType Ty,
                      const ABIArgInfo &AI);

} // namespace neverc::CodeGen

#endif // NEVERC_LIB_EMIT_ABI_ABIINFOIMPL_H
