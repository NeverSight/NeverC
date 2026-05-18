#ifndef NEVERC_SEMA_WEAK_H
#define NEVERC_SEMA_WEAK_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/ADT/DenseMapInfo.h"

namespace neverc {

class IdentifierInfo;

class WeakInfo {
  const IdentifierInfo *alias = nullptr; // alias (optional)
  SourceLocation loc;                    // for diagnostics
public:
  WeakInfo() = default;
  WeakInfo(const IdentifierInfo *Alias, SourceLocation Loc)
      : alias(Alias), loc(Loc) {}
  inline const IdentifierInfo *getAlias() const { return alias; }
  inline SourceLocation getLocation() const { return loc; }
  bool operator==(WeakInfo RHS) const = delete;
  bool operator!=(WeakInfo RHS) const = delete;

  struct DenseMapInfoByAliasOnly
      : private llvm::DenseMapInfo<const IdentifierInfo *> {
    static inline WeakInfo getEmptyKey() {
      return WeakInfo(DenseMapInfo::getEmptyKey(), SourceLocation());
    }
    static inline WeakInfo getTombstoneKey() {
      return WeakInfo(DenseMapInfo::getTombstoneKey(), SourceLocation());
    }
    static unsigned getHashValue(const WeakInfo &W) {
      return DenseMapInfo::getHashValue(W.getAlias());
    }
    static bool isEqual(const WeakInfo &LHS, const WeakInfo &RHS) {
      return DenseMapInfo::isEqual(LHS.getAlias(), RHS.getAlias());
    }
  };
};

} // end namespace neverc

#endif // NEVERC_SEMA_WEAK_H
