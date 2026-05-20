#ifndef NEVERC_ANALYZE_DESIGNATOR_H
#define NEVERC_ANALYZE_DESIGNATOR_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {

class Expr;
class IdentifierInfo;

class Designator {
  struct FieldDesignatorInfo {
    /// Refers to the field being initialized.
    const IdentifierInfo *FieldName;

    /// The location of the '.' in the designated initializer.
    SourceLocation DotLoc;

    /// The location of the field name in the designated initializer.
    SourceLocation FieldLoc;

    FieldDesignatorInfo(const IdentifierInfo *FieldName, SourceLocation DotLoc,
                        SourceLocation FieldLoc)
        : FieldName(FieldName), DotLoc(DotLoc), FieldLoc(FieldLoc) {}
  };

  struct ArrayDesignatorInfo {
    Expr *Index;

    // The location of the '[' in the designated initializer.
    SourceLocation LBracketLoc;

    // The location of the ']' in the designated initializer.
    mutable SourceLocation RBracketLoc;

    ArrayDesignatorInfo(Expr *Index, SourceLocation LBracketLoc)
        : Index(Index), LBracketLoc(LBracketLoc) {}
  };

  struct ArrayRangeDesignatorInfo {
    Expr *Start;
    Expr *End;

    // The location of the '[' in the designated initializer.
    SourceLocation LBracketLoc;

    // The location of the '...' in the designated initializer.
    SourceLocation EllipsisLoc;

    // The location of the ']' in the designated initializer.
    mutable SourceLocation RBracketLoc;

    ArrayRangeDesignatorInfo(Expr *Start, Expr *End, SourceLocation LBracketLoc,
                             SourceLocation EllipsisLoc)
        : Start(Start), End(End), LBracketLoc(LBracketLoc),
          EllipsisLoc(EllipsisLoc) {}
  };

  enum DesignatorKind {
    FieldDesignator,
    ArrayDesignator,
    ArrayRangeDesignator
  };

  DesignatorKind Kind;

  union {
    FieldDesignatorInfo FieldInfo;
    ArrayDesignatorInfo ArrayInfo;
    ArrayRangeDesignatorInfo ArrayRangeInfo;
  };

  Designator(DesignatorKind Kind) : Kind(Kind) {}

public:
  bool isFieldDesignator() const { return Kind == FieldDesignator; }
  bool isArrayDesignator() const { return Kind == ArrayDesignator; }
  bool isArrayRangeDesignator() const { return Kind == ArrayRangeDesignator; }

  //===--------------------------------------------------------------------===//
  // FieldDesignatorInfo

  static Designator CreateFieldDesignator(const IdentifierInfo *FieldName,
                                          SourceLocation DotLoc,
                                          SourceLocation FieldLoc) {
    Designator D(FieldDesignator);
    new (&D.FieldInfo) FieldDesignatorInfo(FieldName, DotLoc, FieldLoc);
    return D;
  }

  const IdentifierInfo *getFieldDecl() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return FieldInfo.FieldName;
  }

  SourceLocation getDotLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return FieldInfo.DotLoc;
  }

  SourceLocation getFieldLoc() const {
    assert(isFieldDesignator() && "Invalid accessor");
    return FieldInfo.FieldLoc;
  }

  //===--------------------------------------------------------------------===//
  // ArrayDesignatorInfo:

  static Designator CreateArrayDesignator(Expr *Index,
                                          SourceLocation LBracketLoc) {
    Designator D(ArrayDesignator);
    new (&D.ArrayInfo) ArrayDesignatorInfo(Index, LBracketLoc);
    return D;
  }

  Expr *getArrayIndex() const {
    assert(isArrayDesignator() && "Invalid accessor");
    return ArrayInfo.Index;
  }

  SourceLocation getLBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    return isArrayDesignator() ? ArrayInfo.LBracketLoc
                               : ArrayRangeInfo.LBracketLoc;
  }

  SourceLocation getRBracketLoc() const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    return isArrayDesignator() ? ArrayInfo.RBracketLoc
                               : ArrayRangeInfo.RBracketLoc;
  }

  //===--------------------------------------------------------------------===//
  // ArrayRangeDesignatorInfo:

  static Designator CreateArrayRangeDesignator(Expr *Start, Expr *End,
                                               SourceLocation LBracketLoc,
                                               SourceLocation EllipsisLoc) {
    Designator D(ArrayRangeDesignator);
    new (&D.ArrayRangeInfo)
        ArrayRangeDesignatorInfo(Start, End, LBracketLoc, EllipsisLoc);
    return D;
  }

  Expr *getArrayRangeStart() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.Start;
  }

  Expr *getArrayRangeEnd() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.End;
  }

  SourceLocation getEllipsisLoc() const {
    assert(isArrayRangeDesignator() && "Invalid accessor");
    return ArrayRangeInfo.EllipsisLoc;
  }

  void setRBracketLoc(SourceLocation RBracketLoc) const {
    assert((isArrayDesignator() || isArrayRangeDesignator()) &&
           "Invalid accessor");
    if (isArrayDesignator())
      ArrayInfo.RBracketLoc = RBracketLoc;
    else
      ArrayRangeInfo.RBracketLoc = RBracketLoc;
  }
};

class Designation {
  llvm::SmallVector<Designator, 2> Designators;

public:
  void AddDesignator(Designator D) { Designators.push_back(D); }

  bool empty() const { return Designators.empty(); }

  unsigned getNumDesignators() const { return Designators.size(); }
  const Designator &getDesignator(unsigned Idx) const {
    assert(Idx < Designators.size());
    return Designators[Idx];
  }
};

} // end namespace neverc

#endif
