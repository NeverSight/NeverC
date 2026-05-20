#ifndef NEVERC_TREE_TYPEORDERING_H
#define NEVERC_TREE_TYPEORDERING_H

#include "neverc/Tree/Type/CanonicalType.h"
#include "neverc/Tree/Type/Type.h"
#include <functional>

namespace neverc {

struct QualTypeOrdering {
  bool operator()(QualType T1, QualType T2) const {
    return std::less<void *>()(T1.getAsOpaquePtr(), T2.getAsOpaquePtr());
  }
};

} // namespace neverc

namespace llvm {

template <> struct DenseMapInfo<neverc::QualType> {
  static inline neverc::QualType getEmptyKey() { return neverc::QualType(); }

  static inline neverc::QualType getTombstoneKey() {
    using neverc::QualType;
    return QualType::getFromOpaquePtr(reinterpret_cast<neverc::Type *>(-1));
  }

  static unsigned getHashValue(neverc::QualType Val) {
    return (unsigned)((uintptr_t)Val.getAsOpaquePtr()) ^
           ((unsigned)((uintptr_t)Val.getAsOpaquePtr() >> 9));
  }

  static bool isEqual(neverc::QualType LHS, neverc::QualType RHS) {
    return LHS == RHS;
  }
};

template <> struct DenseMapInfo<neverc::CanQualType> {
  static inline neverc::CanQualType getEmptyKey() {
    return neverc::CanQualType();
  }

  static inline neverc::CanQualType getTombstoneKey() {
    using neverc::CanQualType;
    return CanQualType::getFromOpaquePtr(reinterpret_cast<neverc::Type *>(-1));
  }

  static unsigned getHashValue(neverc::CanQualType Val) {
    return (unsigned)((uintptr_t)Val.getAsOpaquePtr()) ^
           ((unsigned)((uintptr_t)Val.getAsOpaquePtr() >> 9));
  }

  static bool isEqual(neverc::CanQualType LHS, neverc::CanQualType RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

#endif
