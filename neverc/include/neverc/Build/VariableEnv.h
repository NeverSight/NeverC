#ifndef NEVERC_BUILD_VARIABLEENV_H
#define NEVERC_BUILD_VARIABLEENV_H

#include "neverc/Build/AST.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace neverc {
namespace build {

class FunctionRegistry;

class VariableEnv {
public:
  enum class Origin {
    Default,
    Environment,
    File,
    CommandLine,
    Override,
    Automatic,
  };

  struct Variable {
    std::string Value;
    AssignMode Mode = AssignMode::Recursive;
    Origin Orig = Origin::File;
    bool Exported = false;
  };

  VariableEnv();

  void set(const std::string &Name, const std::string &Value,
           AssignMode Mode, Origin Orig = Origin::File);
  void append(const std::string &Name, const std::string &Value);
  void conditionalSet(const std::string &Name, const std::string &Value);
  void setExport(const std::string &Name, bool Export = true);

  std::string get(const std::string &Name);
  std::string expand(const std::string &Expr);
  bool isDefined(const std::string &Name) const;
  std::string rawValue(const std::string &Name) const;
  Origin getOrigin(const std::string &Name) const;
  std::string getFlavor(const std::string &Name) const;

  void setAutoVar(const std::string &Name, const std::string &Value);
  void clearAutoVars();

  void importEnvironment();
  void setCommandLineVar(const std::string &Name, const std::string &Value);

  void setFunctionRegistry(FunctionRegistry *Reg) { FuncReg = Reg; }

  using EvalCallback = std::function<void(const std::string &)>;
  void setEvalCallback(EvalCallback CB) { EvalCB = std::move(CB); }
  void invokeEval(const std::string &Text) {
    if (EvalCB) EvalCB(Text);
  }

  const std::unordered_map<std::string, Variable> &vars() const {
    return Vars;
  }

private:
  std::string expandInternal(const std::string &Expr,
                              std::unordered_set<std::string> &Expanding);
  std::string expandVarRef(const std::string &Expr, size_t &Pos,
                            std::unordered_set<std::string> &Expanding);
  std::string evaluateFunction(const std::string &Name,
                                const std::string &Args,
                                std::unordered_set<std::string> &Expanding);

  std::unordered_map<std::string, Variable> Vars;
  std::unordered_map<std::string, std::string> AutoVars;
  FunctionRegistry *FuncReg = nullptr;
  EvalCallback EvalCB;
  unsigned RecursionDepth = 0;
  static constexpr unsigned MaxRecursionDepth = 256;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_VARIABLEENV_H
