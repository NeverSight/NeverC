#ifndef NEVERC_BUILD_AST_H
#define NEVERC_BUILD_AST_H

#include <memory>
#include <string>
#include <vector>

namespace neverc {
namespace build {

struct Recipe {
  std::string Command;
  bool Silent = false;
  bool IgnoreError = false;
  bool Force = false;
};

enum class AssignMode {
  Recursive,  // =
  Simple,     // := or ::=
  Conditional,// ?=
  Append,     // +=
  Shell,      // !=
};

enum class StmtKind {
  VarAssign,
  Rule,
  Conditional,
  Include,
  DefineBlock,
  ExportDirective,
  UndefineDirective,
  TargetVarAssign,
  Expression,
};

struct Statement {
  const StmtKind Kind;
  explicit Statement(StmtKind K) : Kind(K) {}
  virtual ~Statement() = default;
};

struct VarAssign : Statement {
  std::string Name;
  AssignMode Mode;
  std::string RawValue;
  bool Override = false;
  bool Export = false;

  VarAssign() : Statement(StmtKind::VarAssign) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::VarAssign;
  }
};

struct Rule : Statement {
  std::vector<std::string> Targets;
  std::vector<std::string> Prerequisites;
  std::vector<std::string> OrderOnlyPrereqs;
  std::vector<Recipe> Recipes;
  bool IsPattern = false;
  bool IsStaticPattern = false;
  std::string StaticTargetPattern;
  std::vector<std::string> StaticPrereqPatterns;

  Rule() : Statement(StmtKind::Rule) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::Rule;
  }
};

struct Conditional : Statement {
  enum Kind { IfEq, IfNeq, IfDef, IfNDef };
  Kind CondKind;
  std::string Arg1, Arg2;
  std::vector<std::unique_ptr<Statement>> ThenBranch;
  std::vector<std::unique_ptr<Statement>> ElseBranch;

  Conditional() : Statement(StmtKind::Conditional) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::Conditional;
  }
};

struct Include : Statement {
  std::vector<std::string> Files;
  bool Optional = false;

  Include() : Statement(StmtKind::Include) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::Include;
  }
};

struct DefineBlock : Statement {
  std::string Name;
  AssignMode Mode;
  std::string Body;
  bool Override = false;

  DefineBlock() : Statement(StmtKind::DefineBlock) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::DefineBlock;
  }
};

struct ExportDirective : Statement {
  std::vector<std::string> Names;
  bool ExportAll = false;
  bool IsUnexport = false;

  ExportDirective() : Statement(StmtKind::ExportDirective) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::ExportDirective;
  }
};

struct UndefineDirective : Statement {
  std::string Name;
  bool Override = false;

  UndefineDirective() : Statement(StmtKind::UndefineDirective) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::UndefineDirective;
  }
};

struct TargetVarAssign : Statement {
  std::vector<std::string> Targets;
  std::string VarName;
  AssignMode Mode;
  std::string RawValue;
  bool Override = false;

  TargetVarAssign() : Statement(StmtKind::TargetVarAssign) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::TargetVarAssign;
  }
};

struct Expression : Statement {
  std::string Text;

  Expression() : Statement(StmtKind::Expression) {}
  static bool classof(const Statement *S) {
    return S->Kind == StmtKind::Expression;
  }
};

struct MakefileAST {
  std::vector<std::unique_ptr<Statement>> Stmts;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_AST_H
