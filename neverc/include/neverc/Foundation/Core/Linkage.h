#ifndef NEVERC_BASIC_LINKAGE_H
#define NEVERC_BASIC_LINKAGE_H

#include "llvm/Support/ErrorHandling.h"
#include <utility>

namespace neverc {

enum class Linkage : unsigned char {
  // Linkage hasn't been computed.
  Invalid = 0,

  None,

  Internal,

  UniqueExternal,

  VisibleNone,

  External
};

enum LanguageLinkage { CLanguageLinkage, NoLanguageLinkage };

enum GVALinkage {
  GVA_Internal,
  GVA_AvailableExternally,
  GVA_DiscardableODR,
  GVA_StrongExternal,
  GVA_StrongODR
};

inline bool isDiscardableGVALinkage(GVALinkage L) {
  return L <= GVA_DiscardableODR;
}

inline bool isUniqueGVALinkage(GVALinkage L) {
  return L == GVA_Internal || L == GVA_StrongExternal;
}

inline bool isExternallyVisible(Linkage L) {
  switch (L) {
  case Linkage::Invalid:
    llvm_unreachable("Linkage hasn't been computed!");
  case Linkage::None:
  case Linkage::Internal:
  case Linkage::UniqueExternal:
    return false;
  case Linkage::VisibleNone:
  case Linkage::External:
    return true;
  }
  llvm_unreachable("Unhandled Linkage enum");
}

inline Linkage getFormalLinkage(Linkage L) {
  switch (L) {
  case Linkage::UniqueExternal:
    return Linkage::External;
  case Linkage::VisibleNone:
    return Linkage::None;
  default:
    return L;
  }
}

inline bool isExternalFormalLinkage(Linkage L) {
  return getFormalLinkage(L) == Linkage::External;
}

inline Linkage minLinkage(Linkage L1, Linkage L2) {
  if (L2 == Linkage::VisibleNone)
    std::swap(L1, L2);
  if (L1 == Linkage::VisibleNone) {
    if (L2 == Linkage::Internal)
      return Linkage::None;
    if (L2 == Linkage::UniqueExternal)
      return Linkage::None;
  }
  return L1 < L2 ? L1 : L2;
}

} // namespace neverc

#endif // NEVERC_BASIC_LINKAGE_H
