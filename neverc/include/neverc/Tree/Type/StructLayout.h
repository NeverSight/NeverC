#ifndef NEVERC_AST_RECORDLAYOUT_H
#define NEVERC_AST_RECORDLAYOUT_H

#include "neverc/Tree/Core/CharUnits.h"
#include "neverc/Tree/Core/TreeVector.h"
#include "llvm/ADT/ArrayRef.h"
#include <cstdint>

namespace neverc {

class TreeContext;

class StructRecordLayout {
  friend class TreeContext;

  CharUnits Size;

  CharUnits DataSize;

  // Alignment - Alignment of record in characters.
  CharUnits Alignment;

  // UnadjustedAlignment - Maximum of the alignments of the record members in
  // characters.
  CharUnits UnadjustedAlignment;

  TreeVector<uint64_t> FieldOffsets;

  StructRecordLayout(const TreeContext &Ctx, CharUnits size,
                     CharUnits alignment, CharUnits unadjustedAlignment,
                     CharUnits datasize, llvm::ArrayRef<uint64_t> fieldoffsets);

  ~StructRecordLayout() = default;

  void Destroy(TreeContext &Ctx);

public:
  StructRecordLayout(const StructRecordLayout &) = delete;
  StructRecordLayout &operator=(const StructRecordLayout &) = delete;

  CharUnits getAlignment() const { return Alignment; }

  CharUnits getUnadjustedAlignment() const { return UnadjustedAlignment; }

  CharUnits getSize() const { return Size; }

  unsigned getFieldCount() const { return FieldOffsets.size(); }

  uint64_t getFieldOffset(unsigned FieldNo) const {
    return FieldOffsets[FieldNo];
  }

  CharUnits getDataSize() const { return DataSize; }
};

} // namespace neverc

#endif // NEVERC_AST_RECORDLAYOUT_H
