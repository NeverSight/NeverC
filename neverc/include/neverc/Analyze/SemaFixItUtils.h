#ifndef NEVERC_SEMA_SEMAFIXITUTILS_H
#define NEVERC_SEMA_SEMAFIXITUTILS_H

#include "neverc/Tree/Expr/Expr.h"

namespace neverc {

enum OverloadFixItKind {
  OFIK_Undefined = 0,
  OFIK_Dereference,
  OFIK_TakeAddress,
  OFIK_RemoveDereference,
  OFIK_RemoveTakeAddress
};

class Sema;

struct ConversionFixItGenerator {
  static bool compareTypesSimple(CanQualType From, CanQualType To, Sema &S,
                                 SourceLocation Loc, ExprValueKind FromVK);

  std::vector<FixItHint> Hints;

  unsigned NumConversionsFixed;

  OverloadFixItKind Kind;

  typedef bool (*TypeComparisonFuncTy)(const CanQualType FromTy,
                                       const CanQualType ToTy, Sema &S,
                                       SourceLocation Loc,
                                       ExprValueKind FromVK);
  TypeComparisonFuncTy CompareTypes;

  ConversionFixItGenerator(TypeComparisonFuncTy Foo)
      : NumConversionsFixed(0), Kind(OFIK_Undefined), CompareTypes(Foo) {}

  ConversionFixItGenerator()
      : NumConversionsFixed(0), Kind(OFIK_Undefined),
        CompareTypes(compareTypesSimple) {}

  void setConversionChecker(TypeComparisonFuncTy Foo) { CompareTypes = Foo; }

  bool tryToFixConversion(const Expr *FromExpr, const QualType FromQTy,
                          const QualType ToQTy, Sema &S);

  void clear() {
    Hints.clear();
    NumConversionsFixed = 0;
  }

  bool isNull() { return (NumConversionsFixed == 0); }
};

} // namespace neverc
#endif
