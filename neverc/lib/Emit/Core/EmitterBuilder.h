#ifndef NEVERC_LIB_CODEGEN_CORE_CGBUILDER_H
#define NEVERC_LIB_CODEGEN_CORE_CGBUILDER_H

#include "Core/Address.h"
#include "Core/TypeEmitterCache.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Type.h"

namespace neverc {
namespace Emit {

class FunctionEmitter;

class CGBuilderInserter final : public llvm::IRBuilderDefaultInserter {
public:
  CGBuilderInserter() = default;
  explicit CGBuilderInserter(FunctionEmitter *FE) : FE(FE) {}

  void InsertHelper(llvm::Instruction *I, const llvm::Twine &Name,
                    llvm::BasicBlock *BB,
                    llvm::BasicBlock::iterator InsertPt) const override;

private:
  FunctionEmitter *FE = nullptr;
};

typedef CGBuilderInserter CGBuilderInserterTy;

typedef llvm::IRBuilder<llvm::ConstantFolder, CGBuilderInserterTy>
    CGBuilderBaseTy;

class CGBuilderTy : public CGBuilderBaseTy {
  const TypeEmitterCache &TypeCache;

public:
  CGBuilderTy(const TypeEmitterCache &TypeCache, llvm::LLVMContext &C)
      : CGBuilderBaseTy(C), TypeCache(TypeCache) {}
  CGBuilderTy(const TypeEmitterCache &TypeCache, llvm::LLVMContext &C,
              const llvm::ConstantFolder &F,
              const CGBuilderInserterTy &Inserter)
      : CGBuilderBaseTy(C, F, Inserter), TypeCache(TypeCache) {}
  CGBuilderTy(const TypeEmitterCache &TypeCache, llvm::Instruction *I)
      : CGBuilderBaseTy(I), TypeCache(TypeCache) {}
  CGBuilderTy(const TypeEmitterCache &TypeCache, llvm::BasicBlock *BB)
      : CGBuilderBaseTy(BB), TypeCache(TypeCache) {}

  llvm::ConstantInt *getSize(CharUnits N) {
    return llvm::ConstantInt::get(TypeCache.SizeTy, N.getQuantity());
  }
  llvm::ConstantInt *getSize(uint64_t N) {
    return llvm::ConstantInt::get(TypeCache.SizeTy, N);
  }

  // Note that we intentionally hide the CreateLoad APIs that don't
  // take an alignment.
  llvm::LoadInst *CreateLoad(Address Addr, const llvm::Twine &Name = "") {
    return CreateAlignedLoad(Addr.getElementType(), Addr.getPointer(),
                             Addr.getAlignment().getAsAlign(), Name);
  }
  llvm::LoadInst *CreateLoad(Address Addr, const char *Name) {
    // This overload is required to prevent string literals from
    // ending up in the IsVolatile overload.
    return CreateAlignedLoad(Addr.getElementType(), Addr.getPointer(),
                             Addr.getAlignment().getAsAlign(), Name);
  }
  llvm::LoadInst *CreateLoad(Address Addr, bool IsVolatile,
                             const llvm::Twine &Name = "") {
    return CreateAlignedLoad(Addr.getElementType(), Addr.getPointer(),
                             Addr.getAlignment().getAsAlign(), IsVolatile,
                             Name);
  }

  using CGBuilderBaseTy::CreateAlignedLoad;
  llvm::LoadInst *CreateAlignedLoad(llvm::Type *Ty, llvm::Value *Addr,
                                    CharUnits Align,
                                    const llvm::Twine &Name = "") {
    return CreateAlignedLoad(Ty, Addr, Align.getAsAlign(), Name);
  }

  // Note that we intentionally hide the CreateStore APIs that don't
  // take an alignment.
  llvm::StoreInst *CreateStore(llvm::Value *Val, Address Addr,
                               bool IsVolatile = false) {
    return CreateAlignedStore(Val, Addr.getPointer(),
                              Addr.getAlignment().getAsAlign(), IsVolatile);
  }

  using CGBuilderBaseTy::CreateAlignedStore;
  llvm::StoreInst *CreateAlignedStore(llvm::Value *Val, llvm::Value *Addr,
                                      CharUnits Align,
                                      bool IsVolatile = false) {
    return CreateAlignedStore(Val, Addr, Align.getAsAlign(), IsVolatile);
  }

  llvm::StoreInst *CreateDefaultAlignedStore(llvm::Value *Val,
                                             llvm::Value *Addr,
                                             bool IsVolatile = false) {
    return CGBuilderBaseTy::CreateStore(Val, Addr, IsVolatile);
  }

  llvm::LoadInst *CreateFlagLoad(llvm::Value *Addr,
                                 const llvm::Twine &Name = "") {
    return CreateAlignedLoad(getInt1Ty(), Addr, CharUnits::One(), Name);
  }

  llvm::StoreInst *CreateFlagStore(bool Value, llvm::Value *Addr) {
    return CreateAlignedStore(getInt1(Value), Addr, CharUnits::One());
  }

  llvm::AtomicCmpXchgInst *
  CreateAtomicCmpXchg(Address Addr, llvm::Value *Cmp, llvm::Value *New,
                      llvm::AtomicOrdering SuccessOrdering,
                      llvm::AtomicOrdering FailureOrdering,
                      llvm::SyncScope::ID SSID = llvm::SyncScope::System) {
    return CGBuilderBaseTy::CreateAtomicCmpXchg(
        Addr.getPointer(), Cmp, New, Addr.getAlignment().getAsAlign(),
        SuccessOrdering, FailureOrdering, SSID);
  }

  llvm::AtomicRMWInst *
  CreateAtomicRMW(llvm::AtomicRMWInst::BinOp Op, Address Addr, llvm::Value *Val,
                  llvm::AtomicOrdering Ordering,
                  llvm::SyncScope::ID SSID = llvm::SyncScope::System) {
    return CGBuilderBaseTy::CreateAtomicRMW(Op, Addr.getPointer(), Val,
                                            Addr.getAlignment().getAsAlign(),
                                            Ordering, SSID);
  }

  using CGBuilderBaseTy::CreateAddrSpaceCast;
  Address CreateAddrSpaceCast(Address Addr, llvm::Type *Ty,
                              const llvm::Twine &Name = "") {
    return Addr.withPointer(CreateAddrSpaceCast(Addr.getPointer(), Ty, Name),
                            Addr.isKnownNonNull());
  }

  using CGBuilderBaseTy::CreatePointerBitCastOrAddrSpaceCast;
  Address CreatePointerBitCastOrAddrSpaceCast(Address Addr, llvm::Type *Ty,
                                              llvm::Type *ElementTy,
                                              const llvm::Twine &Name = "") {
    llvm::Value *Ptr =
        CreatePointerBitCastOrAddrSpaceCast(Addr.getPointer(), Ty, Name);
    return Address(Ptr, ElementTy, Addr.getAlignment(), Addr.isKnownNonNull());
  }

  using CGBuilderBaseTy::CreateStructGEP;
  Address CreateStructGEP(Address Addr, unsigned Index,
                          const llvm::Twine &Name = "") {
    llvm::StructType *ElTy = cast<llvm::StructType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    const llvm::StructLayout *Layout = DL.getStructLayout(ElTy);
    auto Offset = CharUnits::fromQuantity(Layout->getElementOffset(Index));

    return Address(
        CreateStructGEP(Addr.getElementType(), Addr.getPointer(), Index, Name),
        ElTy->getElementType(Index),
        Addr.getAlignment().alignmentAtOffset(Offset), Addr.isKnownNonNull());
  }

  Address CreateConstArrayGEP(Address Addr, uint64_t Index,
                              const llvm::Twine &Name = "") {
    llvm::ArrayType *ElTy = cast<llvm::ArrayType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    CharUnits EltSize =
        CharUnits::fromQuantity(DL.getTypeAllocSize(ElTy->getElementType()));

    return Address(
        CreateInBoundsGEP(Addr.getElementType(), Addr.getPointer(),
                          {getSize(CharUnits::Zero()), getSize(Index)}, Name),
        ElTy->getElementType(),
        Addr.getAlignment().alignmentAtOffset(Index * EltSize),
        Addr.isKnownNonNull());
  }

  Address CreateConstInBoundsGEP(Address Addr, uint64_t Index,
                                 const llvm::Twine &Name = "") {
    llvm::Type *ElTy = Addr.getElementType();
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    CharUnits EltSize = CharUnits::fromQuantity(DL.getTypeAllocSize(ElTy));

    return Address(CreateInBoundsGEP(Addr.getElementType(), Addr.getPointer(),
                                     getSize(Index), Name),
                   ElTy, Addr.getAlignment().alignmentAtOffset(Index * EltSize),
                   Addr.isKnownNonNull());
  }

  Address CreateConstGEP(Address Addr, uint64_t Index,
                         const llvm::Twine &Name = "") {
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    CharUnits EltSize =
        CharUnits::fromQuantity(DL.getTypeAllocSize(Addr.getElementType()));

    return Address(CreateGEP(Addr.getElementType(), Addr.getPointer(),
                             getSize(Index), Name),
                   Addr.getElementType(),
                   Addr.getAlignment().alignmentAtOffset(Index * EltSize),
                   NotKnownNonNull);
  }

  using CGBuilderBaseTy::CreateGEP;
  Address CreateGEP(Address Addr, llvm::Value *Index,
                    const llvm::Twine &Name = "") {
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    CharUnits EltSize =
        CharUnits::fromQuantity(DL.getTypeAllocSize(Addr.getElementType()));

    return Address(
        CreateGEP(Addr.getElementType(), Addr.getPointer(), Index, Name),
        Addr.getElementType(),
        Addr.getAlignment().alignmentOfArrayElement(EltSize), NotKnownNonNull);
  }

  Address CreateConstInBoundsByteGEP(Address Addr, CharUnits Offset,
                                     const llvm::Twine &Name = "") {
    assert(Addr.getElementType() == TypeCache.Int8Ty);
    return Address(CreateInBoundsGEP(Addr.getElementType(), Addr.getPointer(),
                                     getSize(Offset), Name),
                   Addr.getElementType(),
                   Addr.getAlignment().alignmentAtOffset(Offset),
                   Addr.isKnownNonNull());
  }
  Address CreateConstByteGEP(Address Addr, CharUnits Offset,
                             const llvm::Twine &Name = "") {
    assert(Addr.getElementType() == TypeCache.Int8Ty);
    return Address(CreateGEP(Addr.getElementType(), Addr.getPointer(),
                             getSize(Offset), Name),
                   Addr.getElementType(),
                   Addr.getAlignment().alignmentAtOffset(Offset),
                   NotKnownNonNull);
  }

  using CGBuilderBaseTy::CreateConstInBoundsGEP2_32;
  Address CreateConstInBoundsGEP2_32(Address Addr, unsigned Idx0, unsigned Idx1,
                                     const llvm::Twine &Name = "") {
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();

    auto *GEP = cast<llvm::GetElementPtrInst>(CreateConstInBoundsGEP2_32(
        Addr.getElementType(), Addr.getPointer(), Idx0, Idx1, Name));
    llvm::APInt Offset(
        DL.getIndexSizeInBits(Addr.getType()->getPointerAddressSpace()), 0,
        /*isSigned=*/true);
    if (!GEP->accumulateConstantOffset(DL, Offset))
      llvm_unreachable("offset of GEP with constants is always computable");
    return Address(GEP, GEP->getResultElementType(),
                   Addr.getAlignment().alignmentAtOffset(
                       CharUnits::fromQuantity(Offset.getSExtValue())),
                   Addr.isKnownNonNull());
  }

  using CGBuilderBaseTy::CreateMemCpy;
  llvm::CallInst *CreateMemCpy(Address Dest, Address Src, llvm::Value *Size,
                               bool IsVolatile = false) {
    return CreateMemCpy(Dest.getPointer(), Dest.getAlignment().getAsAlign(),
                        Src.getPointer(), Src.getAlignment().getAsAlign(), Size,
                        IsVolatile);
  }
  llvm::CallInst *CreateMemCpy(Address Dest, Address Src, uint64_t Size,
                               bool IsVolatile = false) {
    return CreateMemCpy(Dest.getPointer(), Dest.getAlignment().getAsAlign(),
                        Src.getPointer(), Src.getAlignment().getAsAlign(), Size,
                        IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemCpyInline;
  llvm::CallInst *CreateMemCpyInline(Address Dest, Address Src, uint64_t Size) {
    return CreateMemCpyInline(
        Dest.getPointer(), Dest.getAlignment().getAsAlign(), Src.getPointer(),
        Src.getAlignment().getAsAlign(), getInt64(Size));
  }

  using CGBuilderBaseTy::CreateMemMove;
  llvm::CallInst *CreateMemMove(Address Dest, Address Src, llvm::Value *Size,
                                bool IsVolatile = false) {
    return CreateMemMove(Dest.getPointer(), Dest.getAlignment().getAsAlign(),
                         Src.getPointer(), Src.getAlignment().getAsAlign(),
                         Size, IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemSet;
  llvm::CallInst *CreateMemSet(Address Dest, llvm::Value *Value,
                               llvm::Value *Size, bool IsVolatile = false) {
    return CreateMemSet(Dest.getPointer(), Value, Size,
                        Dest.getAlignment().getAsAlign(), IsVolatile);
  }

  using CGBuilderBaseTy::CreateMemSetInline;
  llvm::CallInst *CreateMemSetInline(Address Dest, llvm::Value *Value,
                                     uint64_t Size) {
    return CreateMemSetInline(Dest.getPointer(),
                              Dest.getAlignment().getAsAlign(), Value,
                              getInt64(Size));
  }

  using CGBuilderBaseTy::CreatePreserveStructAccessIndex;
  Address CreatePreserveStructAccessIndex(Address Addr, unsigned Index,
                                          unsigned FieldIndex,
                                          llvm::MDNode *DbgInfo) {
    llvm::StructType *ElTy = cast<llvm::StructType>(Addr.getElementType());
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
    const llvm::StructLayout *Layout = DL.getStructLayout(ElTy);
    auto Offset = CharUnits::fromQuantity(Layout->getElementOffset(Index));

    return Address(CreatePreserveStructAccessIndex(ElTy, Addr.getPointer(),
                                                   Index, FieldIndex, DbgInfo),
                   ElTy->getElementType(Index),
                   Addr.getAlignment().alignmentAtOffset(Offset));
  }

  using CGBuilderBaseTy::CreateLaunderInvariantGroup;
  Address CreateLaunderInvariantGroup(Address Addr) {
    return Addr.withPointer(CreateLaunderInvariantGroup(Addr.getPointer()),
                            Addr.isKnownNonNull());
  }
};

} // end namespace Emit
} // end namespace neverc

#endif
