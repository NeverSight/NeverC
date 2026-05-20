#ifndef NEVERC_ANALYZE_CLEANUPINFO_H
#define NEVERC_ANALYZE_CLEANUPINFO_H

namespace neverc {

class CleanupInfo {
  bool ExprNeedsCleanups = false;
  bool CleanupsHaveSideEffects = false;

public:
  bool exprNeedsCleanups() const { return ExprNeedsCleanups; }

  bool cleanupsHaveSideEffects() const { return CleanupsHaveSideEffects; }

  void setExprNeedsCleanups(bool SideEffects) {
    ExprNeedsCleanups = true;
    CleanupsHaveSideEffects |= SideEffects;
  }

  void reset() {
    ExprNeedsCleanups = false;
    CleanupsHaveSideEffects = false;
  }

  void mergeFrom(CleanupInfo Rhs) {
    ExprNeedsCleanups |= Rhs.ExprNeedsCleanups;
    CleanupsHaveSideEffects |= Rhs.CleanupsHaveSideEffects;
  }
};

} // end namespace neverc

#endif
