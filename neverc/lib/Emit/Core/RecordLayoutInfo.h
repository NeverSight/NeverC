#ifndef NEVERC_LIB_CODEGEN_CORE_CGRECORDLAYOUT_H
#define NEVERC_LIB_CODEGEN_CORE_CGRECORDLAYOUT_H

#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Decl/Decl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class StructType;
}

namespace neverc {
namespace Emit {

struct BitFieldInfo {
  unsigned Offset : 16;

  unsigned Size : 15;

  unsigned IsSigned : 1;

  unsigned StorageSize;

  CharUnits StorageOffset;

  unsigned VolatileOffset : 16;

  unsigned VolatileStorageSize;

  CharUnits VolatileStorageOffset;

  BitFieldInfo()
      : Offset(), Size(), IsSigned(), StorageSize(), VolatileOffset(),
        VolatileStorageSize() {}

  BitFieldInfo(unsigned Offset, unsigned Size, bool IsSigned,
               unsigned StorageSize, CharUnits StorageOffset)
      : Offset(Offset), Size(Size), IsSigned(IsSigned),
        StorageSize(StorageSize), StorageOffset(StorageOffset) {}

  void print(llvm::raw_ostream &OS) const;
  void dump() const;

  static BitFieldInfo MakeInfo(class TypeEmitter &Types, const FieldDecl *FD,
                               uint64_t Offset, uint64_t Size,
                               uint64_t StorageSize, CharUnits StorageOffset);
};

class RecordLayoutInfo {
  friend class TypeEmitter;

  RecordLayoutInfo(const RecordLayoutInfo &) = delete;
  void operator=(const RecordLayoutInfo &) = delete;

private:
  llvm::StructType *CompleteObjectType;

  llvm::DenseMap<const FieldDecl *, unsigned> FieldInfo;

  llvm::DenseMap<const FieldDecl *, BitFieldInfo> BitFields;

  bool IsZeroInitializable : 1;

public:
  RecordLayoutInfo(llvm::StructType *CompleteObjectType,
                   bool IsZeroInitializable)
      : CompleteObjectType(CompleteObjectType),
        IsZeroInitializable(IsZeroInitializable) {}

  llvm::StructType *getLLVMType() const { return CompleteObjectType; }

  bool isZeroInitializable() const { return IsZeroInitializable; }

  unsigned getLLVMFieldNo(const FieldDecl *FD) const {
    FD = FD->getCanonicalDecl();
    assert(FieldInfo.contains(FD) && "Invalid field for record!");
    return FieldInfo.lookup(FD);
  }

  const BitFieldInfo &getBitFieldInfo(const FieldDecl *FD) const {
    FD = FD->getCanonicalDecl();
    assert(FD->isBitField() && "Invalid call for non-bit-field decl!");
    llvm::DenseMap<const FieldDecl *, BitFieldInfo>::const_iterator it =
        BitFields.find(FD);
    assert(it != BitFields.end() && "Unable to find bitfield info");
    return it->second;
  }

  void print(llvm::raw_ostream &OS) const;
  void dump() const;
};

} // end namespace Emit
} // end namespace neverc

#endif
