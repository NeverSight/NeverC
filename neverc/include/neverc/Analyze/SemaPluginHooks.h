#ifndef NEVERC_ANALYZE_SEMAPLUGINHOOKS_H
#define NEVERC_ANALYZE_SEMAPLUGINHOOKS_H

#include "neverc/Foundation/Core/SourceLocation.h"
#include "neverc/Foundation/Core/TokenKinds.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace neverc {

class Expr;
class NamedDecl;
class QualType;
class Sema;
class Stmt;

enum class SemaPluginOutcome {
  NotHandled,
  Handled,
  Error,
};

class SemaPluginHooks {
public:
  virtual ~SemaPluginHooks() = default;

  virtual SemaPluginOutcome analyzeBinaryExpression(
      Sema &SemanticAnalyzer, SourceLocation OperatorLocation,
      tok::TokenKind Operator, Expr *Left, Expr *Right,
      Expr *&Replacement) = 0;
  virtual SemaPluginOutcome analyzeCompoundStatement(
      Sema &SemanticAnalyzer, SourceLocation LeftBrace,
      SourceLocation RightBrace, llvm::ArrayRef<Stmt *> Statements,
      Stmt *&Replacement) = 0;
  virtual SemaPluginOutcome analyzeDeclarationReference(
      Sema &SemanticAnalyzer, SourceLocation NameLocation,
      NamedDecl *Declaration, NamedDecl *&Replacement) = 0;
  virtual SemaPluginOutcome analyzeTypeName(
      Sema &SemanticAnalyzer, SourceLocation NameLocation,
      llvm::StringRef Name, QualType &Replacement) = 0;
  virtual SemaPluginOutcome analyzeLookup(
      Sema &SemanticAnalyzer, SourceLocation NameLocation,
      llvm::StringRef Name,
      llvm::SmallVectorImpl<NamedDecl *> &Candidates) = 0;
  virtual SemaPluginOutcome analyzeImplicitConversion(
      Sema &SemanticAnalyzer, Expr *Expression, QualType DestinationType,
      unsigned ConversionContext, Expr *&Replacement) = 0;
};

} // namespace neverc

#endif
