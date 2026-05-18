#ifndef NEVERC_LIB_AST_CORE_LINKAGE_H
#define NEVERC_LIB_AST_CORE_LINKAGE_H

#include "neverc/Tree/Core/TreeFwd.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Decl/DeclC.h"
#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include <optional>

namespace neverc {

struct LVComputationKind {
  unsigned ExplicitKind : 1;
  unsigned IgnoreExplicitVisibility : 1;
  unsigned IgnoreAllVisibility : 1;

  enum { NumLVComputationKindBits = 3 };

  explicit LVComputationKind(NamedDecl::ExplicitVisibilityKind EK)
      : ExplicitKind(EK), IgnoreExplicitVisibility(false),
        IgnoreAllVisibility(false) {}

  NamedDecl::ExplicitVisibilityKind getExplicitVisibilityKind() const {
    return static_cast<NamedDecl::ExplicitVisibilityKind>(ExplicitKind);
  }

  bool isTypeVisibility() const {
    return getExplicitVisibilityKind() == NamedDecl::VisibilityForType;
  }
  bool isValueVisibility() const {
    return getExplicitVisibilityKind() == NamedDecl::VisibilityForValue;
  }

  static LVComputationKind forLinkageOnly() {
    LVComputationKind Result(NamedDecl::VisibilityForValue);
    Result.IgnoreExplicitVisibility = true;
    Result.IgnoreAllVisibility = true;
    return Result;
  }

  unsigned toBits() {
    unsigned Bits = 0;
    Bits = (Bits << 1) | ExplicitKind;
    Bits = (Bits << 1) | IgnoreExplicitVisibility;
    Bits = (Bits << 1) | IgnoreAllVisibility;
    return Bits;
  }
};

class LinkageComputer {
  using QueryType =
      llvm::PointerIntPair<const NamedDecl *,
                           LVComputationKind::NumLVComputationKindBits>;
  llvm::SmallDenseMap<QueryType, LinkageInfo, 8> CachedLinkageInfo;

  static QueryType makeCacheKey(const NamedDecl *ND, LVComputationKind Kind) {
    return QueryType(ND, Kind.toBits());
  }

  std::optional<LinkageInfo> lookup(const NamedDecl *ND,
                                    LVComputationKind Kind) const {
    auto Iter = CachedLinkageInfo.find(makeCacheKey(ND, Kind));
    if (Iter == CachedLinkageInfo.end())
      return std::nullopt;
    return Iter->second;
  }

  void cache(const NamedDecl *ND, LVComputationKind Kind, LinkageInfo Info) {
    CachedLinkageInfo[makeCacheKey(ND, Kind)] = Info;
  }

  LinkageInfo getLVForFileScopeDecl(const NamedDecl *D,
                                    LVComputationKind computation,
                                    bool IgnoreVarTypeLinkage);

  LinkageInfo getLVForRecordMember(const NamedDecl *D,
                                   LVComputationKind computation,
                                   bool IgnoreVarTypeLinkage);

  LinkageInfo getLVForLocalDecl(const NamedDecl *D,
                                LVComputationKind computation);

  LinkageInfo getLVForType(const Type &T, LVComputationKind computation);

  LinkageInfo getLVForValue(const APValue &V, LVComputationKind computation);

public:
  LinkageInfo computeLVForDecl(const NamedDecl *D,
                               LVComputationKind computation,
                               bool IgnoreVarTypeLinkage = false);

  LinkageInfo getLVForDecl(const NamedDecl *D, LVComputationKind computation);

  LinkageInfo computeTypeLinkageInfo(const Type *T);
  LinkageInfo computeTypeLinkageInfo(QualType T) {
    return computeTypeLinkageInfo(T.getTypePtr());
  }

  LinkageInfo getDeclLinkageAndVisibility(const NamedDecl *D);

  LinkageInfo getTypeLinkageAndVisibility(const Type *T);
  LinkageInfo getTypeLinkageAndVisibility(QualType T) {
    return getTypeLinkageAndVisibility(T.getTypePtr());
  }
};
} // namespace neverc

#endif
