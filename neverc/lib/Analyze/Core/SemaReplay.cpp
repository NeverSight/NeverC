#include "neverc/Analyze/SemaReplay.h"
#include "neverc/Analyze/Scope.h"
#include "neverc/Analyze/Sema.h"
#include "neverc/Tree/Decl/Decl.h"
#include "neverc/Tree/Expr/Expr.h"
#include "neverc/Tree/Stmt/Stmt.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Twine.h"

using namespace llvm;

namespace neverc {
namespace {

class ReplayWalker {
public:
  ReplayWalker(Sema &SemanticAnalyzer, SemaReplayResult &ResultValue)
      : S(SemanticAnalyzer), Result(ResultValue) {}

  bool validateStatement(Stmt *Statement, Scope &CurrentScope,
                         FunctionDecl &Function) {
    if (!Statement)
      return fail(SemaReplayStatus::InvalidAST,
                  "semantic replay encountered a null statement");
    if (!Visited.insert(Statement).second)
      return fail(SemaReplayStatus::InvalidAST,
                  "semantic replay encountered a statement cycle");

    ++Result.StatementCount;
    if (auto *Compound = dyn_cast<CompoundStmt>(Statement)) {
      for (Stmt *Child : Compound->body())
        if (!validateStatement(Child, CurrentScope, Function))
          return false;
      return true;
    }
    if (auto *Return = dyn_cast<ReturnStmt>(Statement)) {
      Expr *Value = Return->getRetValue();
      if (Value && !validateExpression(Value, CurrentScope))
        return false;
      if (Value &&
          !S.getTreeContext().hasSameType(Value->getType(),
                                          Function.getReturnType()))
        return fail(
            SemaReplayStatus::UnsupportedASTKind,
            "semantic replay does not yet apply return-value conversions");
      StmtResult Checked =
          S.OnReturnStmt(Return->getReturnLoc(), Value, &CurrentScope);
      return !Checked.isInvalid() && !Function.isInvalidDecl();
    }
    if (isa<NullStmt>(Statement))
      return true;
    return unsupported(Statement->getStmtClassName());
  }

private:
  bool validateExpression(Expr *Expression, Scope &CurrentScope) {
    (void)CurrentScope;
    if (!Expression)
      return fail(SemaReplayStatus::InvalidAST,
                  "semantic replay encountered a null expression");
    ++Result.ExpressionCount;
    if (const auto *Literal = dyn_cast<IntegerLiteral>(Expression)) {
      if (Literal->getType().isNull() ||
          !Literal->getType()->isIntegerType())
        return fail(SemaReplayStatus::InvalidAST,
                    "integer literal has a non-integer replay type");
      return true;
    }
    return unsupported(Expression->getStmtClassName());
  }

  bool unsupported(StringRef Kind) {
    return fail(SemaReplayStatus::UnsupportedASTKind,
                ("semantic replay does not support AST kind '" + Kind + "'")
                    .str());
  }

  bool fail(SemaReplayStatus Status, StringRef Message) {
    if (Result.Status == SemaReplayStatus::Success) {
      Result.Status = Status;
      Result.Message = Message.str();
    }
    return false;
  }

  Sema &S;
  SemaReplayResult &Result;
  SmallPtrSet<const Stmt *, 32> Visited;
};

} // namespace

SemaReplayResult SemaReplay::run(Sema &S,
                                 TranslationUnitDecl &TranslationUnit) {
  SemaReplayResult Result;
  if (&TranslationUnit != S.getTreeContext().getTranslationUnitDecl()) {
    Result.Status = SemaReplayStatus::InvalidAST;
    Result.Message =
        "semantic replay translation unit belongs to another tree context";
    return Result;
  }

  SmallVector<Decl *, 32> Declarations;
  Declarations.append(TranslationUnit.decls_begin(),
                      TranslationUnit.decls_end());
  const unsigned InitialErrors = S.getDiagnostics().getNumErrors();
  Scope TranslationUnitScope(nullptr, Scope::DeclScope, S.getDiagnostics());
  S.OnTranslationUnitScope(&TranslationUnitScope);
  S.Initialize();
  bool Ended = false;
  auto FinishTranslationUnit = make_scope_exit([&] {
    if (!Ended)
      S.OnEndOfTranslationUnit();
  });

  ReplayWalker Walker(S, Result);
  for (Decl *Declaration : Declarations) {
    if (!Declaration || Declaration->getDeclContext() != &TranslationUnit) {
      Result.Status = SemaReplayStatus::InvalidAST;
      Result.Message =
          "semantic replay found a declaration outside its translation unit";
      return Result;
    }

    ++Result.DeclarationCount;
    auto *Function = dyn_cast<FunctionDecl>(Declaration);
    if (!Function) {
      Result.Status = SemaReplayStatus::UnsupportedASTKind;
      Result.Message =
          ("semantic replay does not support declaration kind '" +
           StringRef(Declaration->getDeclKindName()) + "'")
              .str();
      return Result;
    }
    if (!Function->getIdentifier() || Function->getType().isNull() ||
        !Function->getType()->isFunctionType() || Function->hasAttrs() ||
        Function->getNumParams() != 0) {
      Result.Status = SemaReplayStatus::InvalidAST;
      Result.Message =
          "semantic replay found an unsupported function declaration shape";
      return Result;
    }
    if (S.LookupSingleName(&TranslationUnitScope, Function->getDeclName(),
                           Function->getLocation(), ResolveOrdinary)) {
      Result.Status = SemaReplayStatus::InvalidAST;
      Result.Message =
          "semantic replay found a duplicate top-level declaration";
      return Result;
    }
    S.PushOnScopeChains(Function, &TranslationUnitScope,
                        /*AddToContext=*/false);

    Stmt *Body = Function->getBody();
    if (!Body)
      continue;
    Function->setBody(nullptr);
    Scope FunctionScope(&TranslationUnitScope,
                        Scope::FnScope | Scope::DeclScope |
                            Scope::CompoundStmtScope,
                        S.getDiagnostics());
    Decl *Started = S.OnStartOfFunctionDef(&FunctionScope, Function);
    bool Valid = Started == Function &&
                 Walker.validateStatement(Body, FunctionScope, *Function);
    S.OnFinishFunctionBody(Started, Body);
    if (!Valid) {
      if (Result.Status == SemaReplayStatus::Success) {
        Result.Status = SemaReplayStatus::SemanticError;
        Result.Message = "semantic replay rejected a function definition";
      }
      return Result;
    }
  }

  S.OnEndOfTranslationUnit();
  Ended = true;
  if (S.getDiagnostics().getNumErrors() != InitialErrors) {
    Result.Status = SemaReplayStatus::SemanticError;
    Result.Message = "semantic replay emitted semantic diagnostics";
    return Result;
  }
  Result.Message =
      ("replayed " + Twine(Result.DeclarationCount) + " declarations, " +
       Twine(Result.StatementCount) + " statements, and " +
       Twine(Result.ExpressionCount) + " expressions")
          .str();
  return Result;
}

} // namespace neverc
