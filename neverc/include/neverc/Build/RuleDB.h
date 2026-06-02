#ifndef NEVERC_BUILD_RULEDB_H
#define NEVERC_BUILD_RULEDB_H

#include "neverc/Build/AST.h"
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neverc {
namespace build {

class VariableEnv;

struct TargetVarOverride {
  std::string VarName;
  std::string RawValue;
  AssignMode Mode;
};

struct ResolvedRule {
  std::string Target;
  std::vector<std::string> Prerequisites;
  std::vector<std::string> OrderOnlyPrereqs;
  std::vector<Recipe> Recipes;
  bool IsPhony = false;
  std::string Stem;
};

class RuleDB {
public:
  void addRule(const Rule &R, VariableEnv &Env);
  void addPhony(const std::vector<std::string> &Targets);

  const ResolvedRule *findRule(const std::string &Target) const;
  std::vector<const ResolvedRule *> findAllRules(
      const std::string &Target) const;

  bool isPhony(const std::string &Target) const;
  std::string defaultTarget() const;

  void addTargetVar(const std::string &Target, const TargetVarOverride &Var);
  const std::vector<TargetVarOverride> *
  getTargetVars(const std::string &Target) const;

  const std::unordered_map<std::string, std::vector<ResolvedRule>> &
  rules() const {
    return ExplicitRules;
  }

  struct PatternRule {
    std::string TargetPattern;
    std::vector<std::string> PrereqPatterns;
    std::vector<Recipe> Recipes;
    bool matchTarget(const std::string &Target, std::string &Stem) const;
  };

private:
  const ResolvedRule *matchPatternRule(const std::string &Target) const;

  std::unordered_map<std::string, std::vector<ResolvedRule>> ExplicitRules;
  std::vector<PatternRule> PatternRules;
  std::unordered_set<std::string> PhonyTargets;
  std::string FirstTarget;
  mutable std::map<std::string, ResolvedRule> PatternMatchCache;
  std::unordered_map<std::string, std::vector<TargetVarOverride>> TargetVars;
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_RULEDB_H
