#include "NeverCStringCleanupEmitter.h"
#include "Core/ModuleEmitter.h"

namespace neverc {
namespace Emit {

llvm::Function *getOrCreateStringFreeHelper(llvm::Module &M) {
  if (llvm::Function *F =
          M.getFunction(BuiltinStringNames::CleanupFunctionName))
    return F;

  if (llvm::Function *F =
          M.getFunction(BuiltinStringNames::CompositeCleanupHelperName))
    return F;

  llvm::LLVMContext &Ctx = M.getContext();
  llvm::PointerType *PtrTy = llvm::PointerType::getUnqual(Ctx);

  std::string IRStructName =
      ("struct." + BuiltinStringNames::RecordName).str();
  llvm::StructType *StrTy =
      llvm::StructType::getTypeByName(Ctx, IRStructName);
  if (!StrTy || StrTy->getNumElements() < 3)
    return nullptr;

  llvm::Type *CapTy = StrTy->getElementType(2);

  auto *FTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), {PtrTy}, false);
  llvm::Function *F = llvm::Function::Create(
      FTy, llvm::GlobalValue::InternalLinkage,
      BuiltinStringNames::CompositeCleanupHelperName, &M);
  F->addFnAttr(llvm::Attribute::AlwaysInline);
  F->addFnAttr(llvm::Attribute::NoUnwind);

  llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::BasicBlock *FreeBB = llvm::BasicBlock::Create(Ctx, "do.free", F);
  llvm::BasicBlock *DoneBB = llvm::BasicBlock::Create(Ctx, "done", F);

  llvm::IRBuilder<> B(Entry);
  llvm::Value *Arg = F->getArg(0);
  llvm::Value *CapGEP = B.CreateStructGEP(StrTy, Arg, 2, "cap.ptr");
  llvm::Value *Cap = B.CreateLoad(CapTy, CapGEP, "cap");
  llvm::Value *DataGEP = B.CreateStructGEP(StrTy, Arg, 0, "data.ptr");
  llvm::Value *Data = B.CreateLoad(PtrTy, DataGEP, "data");
  llvm::Value *CapNZ =
      B.CreateICmpNE(Cap, llvm::Constant::getNullValue(CapTy));
  llvm::Value *DataNN =
      B.CreateICmpNE(Data, llvm::ConstantPointerNull::get(PtrTy));
  B.CreateCondBr(B.CreateAnd(CapNZ, DataNN, "need.free"), FreeBB, DoneBB);

  B.SetInsertPoint(FreeBB);
  llvm::FunctionCallee FreeFn = M.getOrInsertFunction(
      "free",
      llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), {PtrTy}, false));
  B.CreateCall(FreeFn, {Data});
  B.CreateBr(DoneBB);

  B.SetInsertPoint(DoneBB);
  B.CreateStore(llvm::ConstantPointerNull::get(PtrTy), DataGEP);
  llvm::Value *LenGEP = B.CreateStructGEP(StrTy, Arg, 1, "len.ptr");
  B.CreateStore(llvm::Constant::getNullValue(StrTy->getElementType(1)), LenGEP);
  B.CreateStore(llvm::Constant::getNullValue(CapTy), CapGEP);
  B.CreateRetVoid();
  return F;
}

void emitStringFieldCleanups(FunctionEmitter &FE, llvm::Value *BaseAddr,
                             QualType T) {
  if (isNeverCStringRecord(T)) {
    if (llvm::Function *CleanFn =
            getOrCreateStringFreeHelper(FE.ME.getModule()))
      FE.Builder.CreateCall(CleanFn, {BaseAddr});
    return;
  }
  if (auto *CAT = FE.getContext().getAsConstantArrayType(T)) {
    uint64_t N = CAT->getSize().getZExtValue();
    QualType ElemTy = CAT->getElementType();
    llvm::Type *ArrTy = FE.convertTypeForMem(T);
    for (uint64_t i = 0; i < N; ++i) {
      llvm::Value *EP =
          FE.Builder.CreateConstInBoundsGEP2_32(ArrTy, BaseAddr, 0, i);
      emitStringFieldCleanups(FE, EP, ElemTy);
    }
    return;
  }
  if (auto *RT = T->getAs<RecordType>()) {
    auto *RD = RT->getDecl()->getDefinition();
    if (!RD)
      return;
    llvm::Type *StructTy = FE.convertTypeForMem(T);
    unsigned Idx = 0;
    for (auto *FD : RD->fields()) {
      llvm::Value *FP = FE.Builder.CreateStructGEP(StructTy, BaseAddr, Idx);
      emitStringFieldCleanups(FE, FP, FD->getType());
      ++Idx;
    }
  }
}

} // namespace Emit
} // namespace neverc
