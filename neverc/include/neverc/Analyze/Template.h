//===--- Template.h - C++ template instantiation ----------------*- C++ -*-===//
#ifndef NEVERC_ANALYZE_TEMPLATE_H
#define NEVERC_ANALYZE_TEMPLATE_H

#include "neverc/Tree/Type/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"

namespace neverc {

class Expr;
class NamedDecl;
class Sema;
class TemplateDecl;
class TreeContext;
class FunctionDecl;
class CXXRecordDecl;
class FunctionTemplateDecl;

/// Template argument (type/expr/pack scaffold).
class TemplateArgument {
public:
  enum ArgKind { Null, Type, Declaration, Integral, Expression, Pack };
  ArgKind Kind = Null;
  QualType TypeArg;
  Expr *ExprArg = nullptr;

  TemplateArgument() = default;
  explicit TemplateArgument(QualType T) : Kind(Type), TypeArg(T) {}
  explicit TemplateArgument(Expr *E) : Kind(Expression), ExprArg(E) {}
  bool isNull() const { return Kind == Null; }
  bool isType() const { return Kind == Type; }
  QualType getAsType() const { return TypeArg; }
  Expr *getAsExpr() const { return ExprArg; }
};

/// Multi-level template argument list.
class MultiLevelTemplateArgumentList {
  llvm::SmallVector<llvm::SmallVector<TemplateArgument, 4>, 4> Levels;

public:
  void addOuterTemplateArguments(llvm::ArrayRef<TemplateArgument> Args) {
    Levels.emplace_back(Args.begin(), Args.end());
  }
  void addInnerTemplateArguments(llvm::ArrayRef<TemplateArgument> Args) {
    Levels.insert(Levels.begin(),
                  llvm::SmallVector<TemplateArgument, 4>(Args.begin(),
                                                         Args.end()));
  }
  unsigned getNumLevels() const { return static_cast<unsigned>(Levels.size()); }
  bool empty() const { return Levels.empty(); }
  llvm::ArrayRef<TemplateArgument> get(unsigned Depth) const {
    return Levels[Depth];
  }
  llvm::ArrayRef<TemplateArgument> getInnermost() const {
    return Levels.empty() ? llvm::ArrayRef<TemplateArgument>()
                          : llvm::ArrayRef<TemplateArgument>(Levels.front());
  }
};

/// Result of substituting into a type under a template argument list.
class TemplateTypeSubstitution {
public:
  QualType Result;
  bool Invalid = false;
  static TemplateTypeSubstitution error() {
    TemplateTypeSubstitution S;
    S.Invalid = true;
    return S;
  }
};

/// Partial ordering scaffold: true if A is at least as specialized as B.
/// Full deduction-based ordering remains incremental.
bool isAtLeastAsSpecialized(Sema &S, FunctionTemplateDecl *A,
                            FunctionTemplateDecl *B);

} // namespace neverc

#endif

