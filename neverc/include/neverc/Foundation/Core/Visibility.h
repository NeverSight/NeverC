#ifndef NEVERC_BASIC_VISIBILITY_H
#define NEVERC_BASIC_VISIBILITY_H

#include "neverc/Foundation/Core/Linkage.h"
#include "llvm/ADT/STLForwardCompat.h"
#include <cassert>
#include <cstdint>

namespace neverc {

enum Visibility {
  HiddenVisibility,

  ProtectedVisibility,

  DefaultVisibility
};

inline Visibility minVisibility(Visibility L, Visibility R) {
  return L < R ? L : R;
}

class LinkageInfo {
  uint8_t linkage_ : 3;
  uint8_t visibility_ : 2;
  uint8_t explicit_ : 1;

  void setVisibility(Visibility V, bool E) {
    visibility_ = V;
    explicit_ = E;
  }

public:
  LinkageInfo()
      : linkage_(llvm::to_underlying(Linkage::External)),
        visibility_(DefaultVisibility), explicit_(false) {}
  LinkageInfo(Linkage L, Visibility V, bool E)
      : linkage_(llvm::to_underlying(L)), visibility_(V), explicit_(E) {
    assert(getLinkage() == L && getVisibility() == V &&
           isVisibilityExplicit() == E && "Enum truncated!");
  }

  static LinkageInfo external() { return LinkageInfo(); }
  static LinkageInfo internal() {
    return LinkageInfo(Linkage::Internal, DefaultVisibility, false);
  }
  static LinkageInfo none() {
    return LinkageInfo(Linkage::None, DefaultVisibility, false);
  }
  static LinkageInfo visible_none() {
    return LinkageInfo(Linkage::VisibleNone, DefaultVisibility, false);
  }

  Linkage getLinkage() const { return static_cast<Linkage>(linkage_); }
  Visibility getVisibility() const { return (Visibility)visibility_; }
  bool isVisibilityExplicit() const { return explicit_; }

  void setLinkage(Linkage L) { linkage_ = llvm::to_underlying(L); }

  void mergeLinkage(Linkage L) { setLinkage(minLinkage(getLinkage(), L)); }
  void mergeLinkage(LinkageInfo other) { mergeLinkage(other.getLinkage()); }

  void mergeExternalVisibility(Linkage L) {
    Linkage ThisL = getLinkage();
    if (!isExternallyVisible(L)) {
      if (ThisL == Linkage::VisibleNone)
        ThisL = Linkage::None;
      else if (ThisL == Linkage::External)
        ThisL = Linkage::UniqueExternal;
    }
    setLinkage(ThisL);
  }
  void mergeExternalVisibility(LinkageInfo Other) {
    mergeExternalVisibility(Other.getLinkage());
  }

  void mergeVisibility(Visibility newVis, bool newExplicit) {
    Visibility oldVis = getVisibility();

    // Never increase visibility.
    if (oldVis < newVis)
      return;

    // If the new visibility is the same as the old and the new
    // visibility isn't explicit, we have nothing to add.
    if (oldVis == newVis && !newExplicit)
      return;

    // Otherwise, we're either decreasing visibility or making our
    // existing visibility explicit.
    setVisibility(newVis, newExplicit);
  }
  void mergeVisibility(LinkageInfo other) {
    mergeVisibility(other.getVisibility(), other.isVisibilityExplicit());
  }

  void merge(LinkageInfo other) {
    mergeLinkage(other);
    mergeVisibility(other);
  }

  void mergeMaybeWithVisibility(LinkageInfo other, bool withVis) {
    mergeLinkage(other);
    if (withVis)
      mergeVisibility(other);
  }
};
} // namespace neverc

#endif // NEVERC_BASIC_VISIBILITY_H
