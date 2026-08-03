//===--- CXXBaseSpecifier.h - C++ base class specifier ----------*- C++ -*-===//
#ifndef NEVERC_TREE_CXXBASESPECIFIER_H
#define NEVERC_TREE_CXXBASESPECIFIER_H

#include "neverc/Basic/Specifiers.h"
#include "neverc/Basic/SourceLocation.h"
#include "neverc/Tree/Type/Type.h"

namespace neverc {

/// Represents a base class of a C++ class.
class CXXBaseSpecifier {
  SourceRange Range;
  SourceLocation EllipsisLoc;
  bool Virtual : 1;
  bool BaseOfClass : 1; // true if base of a class (vs. struct)
  AccessSpecifier Access : 2;
  TypeSourceInfo *BaseTypeInfo;

public:
  CXXBaseSpecifier()
      : Virtual(false), BaseOfClass(true), Access(AS_none),
        BaseTypeInfo(nullptr) {}

  CXXBaseSpecifier(SourceRange R, bool V, bool BC, AccessSpecifier A,
                   TypeSourceInfo *TInfo, SourceLocation Ellipsis = SourceLocation())
      : Range(R), EllipsisLoc(Ellipsis), Virtual(V), BaseOfClass(BC), Access(A),
        BaseTypeInfo(TInfo) {}

  SourceRange getSourceRange() const LLVM_READONLY { return Range; }
  SourceLocation getBeginLoc() const LLVM_READONLY { return Range.getBegin(); }
  SourceLocation getEndLoc() const LLVM_READONLY { return Range.getEnd(); }

  bool isVirtual() const { return Virtual; }
  bool isBaseOfClass() const { return BaseOfClass; }
  bool isPackExpansion() const { return EllipsisLoc.isValid(); }
  SourceLocation getEllipsisLoc() const { return EllipsisLoc; }
  AccessSpecifier getAccessSpecifier() const { return Access; }
  TypeSourceInfo *getTypeSourceInfo() const { return BaseTypeInfo; }
  QualType getType() const {
    return BaseTypeInfo ? BaseTypeInfo->getType() : QualType();
  }
};

} // namespace neverc

#endif
