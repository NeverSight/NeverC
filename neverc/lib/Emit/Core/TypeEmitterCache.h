#ifndef NEVERC_LIB_CODEGEN_CORE_TYPEEMITTERCACHE_H
#define NEVERC_LIB_CODEGEN_CORE_TYPEEMITTERCACHE_H

#include "neverc/Foundation/Core/AddressSpaces.h"
#include "neverc/Tree/Core/CharUnits.h"
#include "llvm/IR/CallingConv.h"

namespace llvm {
class Type;
class IntegerType;
class PointerType;
} // namespace llvm

namespace neverc {
namespace Emit {

struct TypeEmitterCache {
  llvm::Type *VoidTy;

  llvm::IntegerType *Int8Ty, *Int16Ty, *Int32Ty, *Int64Ty;
  llvm::Type *HalfTy, *BFloatTy, *FloatTy, *DoubleTy;

  llvm::IntegerType *IntTy;

  llvm::IntegerType *CharTy;

  union {
    llvm::IntegerType *IntPtrTy;
    llvm::IntegerType *SizeTy;
    llvm::IntegerType *PtrDiffTy;
  };

  union {
    llvm::PointerType *UnqualPtrTy;
    llvm::PointerType *VoidPtrTy;
    llvm::PointerType *Int8PtrTy;
    llvm::PointerType *Int16PtrTy;
    llvm::PointerType *Int32PtrTy;
    llvm::PointerType *Int64PtrTy;
    llvm::PointerType *VoidPtrPtrTy;
    llvm::PointerType *Int8PtrPtrTy;
  };

  union {
    llvm::PointerType *AllocaVoidPtrTy;
    llvm::PointerType *AllocaInt8PtrTy;
  };

  union {
    llvm::PointerType *GlobalsVoidPtrTy;
    llvm::PointerType *GlobalsInt8PtrTy;
  };

  llvm::PointerType *ConstGlobalsPtrTy;

  union {
    unsigned char IntSizeInBytes;
    unsigned char IntAlignInBytes;
  };
  CharUnits getIntSize() const {
    return CharUnits::fromQuantity(IntSizeInBytes);
  }
  CharUnits getIntAlign() const {
    return CharUnits::fromQuantity(IntAlignInBytes);
  }

  unsigned char PointerWidthInBits;

  union {
    unsigned char PointerAlignInBytes;
    unsigned char PointerSizeInBytes;
  };

  union {
    unsigned char SizeSizeInBytes; // sizeof(size_t)
    unsigned char SizeAlignInBytes;
  };

  LangAS ASTAllocaAddressSpace;

  CharUnits getSizeSize() const {
    return CharUnits::fromQuantity(SizeSizeInBytes);
  }
  CharUnits getSizeAlign() const {
    return CharUnits::fromQuantity(SizeAlignInBytes);
  }
  CharUnits getPointerSize() const {
    return CharUnits::fromQuantity(PointerSizeInBytes);
  }
  CharUnits getPointerAlign() const {
    return CharUnits::fromQuantity(PointerAlignInBytes);
  }

  llvm::CallingConv::ID RuntimeCC;
  llvm::CallingConv::ID getRuntimeCC() const { return RuntimeCC; }

  LangAS getASTAllocaAddressSpace() const { return ASTAllocaAddressSpace; }
};

} // end namespace Emit
} // end namespace neverc

#endif
