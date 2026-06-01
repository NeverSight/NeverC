#include "neverc/Build/RuleDB.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/VariableEnv.h"

#include <sstream>

namespace neverc {
namespace build {

static std::vector<std::string> splitWords(const std::string &S) {
  std::vector<std::string> Words;
  std::istringstream SS(S);
  std::string W;
  while (SS >> W)
    Words.push_back(W);
  return Words;
}

bool RuleDB::PatternRule::matchTarget(const std::string &Target,
                                       std::string &Stem) const {
  size_t Pct = TargetPattern.find('%');
  if (Pct == std::string::npos)
    return false;

  std::string Prefix = TargetPattern.substr(0, Pct);
  std::string Suffix = TargetPattern.substr(Pct + 1);

  if (Target.size() < Prefix.size() + Suffix.size())
    return false;
  if (Target.substr(0, Prefix.size()) != Prefix)
    return false;
  if (!Suffix.empty() &&
      Target.substr(Target.size() - Suffix.size()) != Suffix)
    return false;

  Stem = Target.substr(Prefix.size(),
                       Target.size() - Prefix.size() - Suffix.size());
  return true;
}

void RuleDB::addRule(const Rule &R, VariableEnv &Env) {
  for (auto &Target : R.Targets) {
    std::string ExpandedTarget = Env.expand(Target);

    if (ExpandedTarget == ".PHONY") {
      std::vector<std::string> ExpandedPrereqs;
      for (auto &P : R.Prerequisites)
        for (auto &W : splitWords(Env.expand(P)))
          ExpandedPrereqs.push_back(W);
      addPhony(ExpandedPrereqs);
      continue;
    }

    if (ExpandedTarget == ".SUFFIXES")
      continue;
  }

  if (R.IsStaticPattern) {
    std::string TgtPat = Env.expand(R.StaticTargetPattern);
    size_t Pct = TgtPat.find('%');
    std::string Prefix = (Pct != std::string::npos) ? TgtPat.substr(0, Pct) : "";
    std::string Suffix = (Pct != std::string::npos) ? TgtPat.substr(Pct + 1) : "";

    for (auto &Target : R.Targets) {
      std::string ET = Env.expand(Target);
      for (auto &ExpandedTarget : splitWords(ET)) {
        if (ExpandedTarget.size() < Prefix.size() + Suffix.size())
          continue;
        if (!Prefix.empty() &&
            ExpandedTarget.substr(0, Prefix.size()) != Prefix)
          continue;
        if (!Suffix.empty() &&
            ExpandedTarget.substr(ExpandedTarget.size() - Suffix.size()) !=
                Suffix)
          continue;
        std::string Stem = ExpandedTarget.substr(
            Prefix.size(),
            ExpandedTarget.size() - Prefix.size() - Suffix.size());

        ResolvedRule RR;
        RR.Target = ExpandedTarget;
        RR.Stem = Stem;
        RR.IsPhony = PhonyTargets.count(ExpandedTarget) > 0;
        RR.Recipes = R.Recipes;
        for (auto &PP : R.StaticPrereqPatterns) {
          std::string EP = Env.expand(PP);
          size_t PPct = EP.find('%');
          if (PPct != std::string::npos)
            RR.Prerequisites.push_back(
                EP.substr(0, PPct) + Stem + EP.substr(PPct + 1));
          else
            RR.Prerequisites.push_back(EP);
        }
        ExplicitRules[ExpandedTarget].push_back(RR);
        if (FirstTarget.empty() && ExpandedTarget[0] != '.')
          FirstTarget = ExpandedTarget;
      }
    }
    return;
  }

  if (R.IsPattern) {
    PatternRule PR;
    PR.TargetPattern = Env.expand(R.Targets[0]);
    for (auto &P : R.Prerequisites)
      for (auto &W : splitWords(Env.expand(P)))
        PR.PrereqPatterns.push_back(W);
    PR.Recipes = R.Recipes;
    PatternRules.push_back(PR);
    return;
  }

  for (auto &Target : R.Targets) {
    std::string ExpandedTarget = Env.expand(Target);

    if (ExpandedTarget[0] == '.')
      continue;

    ResolvedRule RR;
    RR.Target = ExpandedTarget;
    RR.IsPhony = PhonyTargets.count(ExpandedTarget) > 0;
    RR.Recipes = R.Recipes;

    for (auto &P : R.Prerequisites) {
      for (auto &W : splitWords(Env.expand(P)))
        RR.Prerequisites.push_back(W);
    }
    for (auto &P : R.OrderOnlyPrereqs) {
      for (auto &W : splitWords(Env.expand(P)))
        RR.OrderOnlyPrereqs.push_back(W);
    }

    ExplicitRules[ExpandedTarget].push_back(RR);

    if (FirstTarget.empty() && ExpandedTarget[0] != '.')
      FirstTarget = ExpandedTarget;
  }
}

void RuleDB::addPhony(const std::vector<std::string> &Targets) {
  for (auto &T : Targets) {
    PhonyTargets.insert(T);
    auto It = ExplicitRules.find(T);
    if (It != ExplicitRules.end())
      for (auto &RR : It->second)
        RR.IsPhony = true;
  }
}

const ResolvedRule *RuleDB::findRule(const std::string &Target) const {
  auto It = ExplicitRules.find(Target);
  if (It != ExplicitRules.end() && !It->second.empty())
    return &It->second[0];

  return matchPatternRule(Target);
}

std::vector<const ResolvedRule *>
RuleDB::findAllRules(const std::string &Target) const {
  std::vector<const ResolvedRule *> Result;
  auto It = ExplicitRules.find(Target);
  if (It != ExplicitRules.end())
    for (auto &RR : It->second)
      Result.push_back(&RR);

  if (Result.empty()) {
    auto *PR = matchPatternRule(Target);
    if (PR)
      Result.push_back(PR);
  }
  return Result;
}

bool RuleDB::isPhony(const std::string &Target) const {
  return PhonyTargets.count(Target) > 0;
}

std::string RuleDB::defaultTarget() const { return FirstTarget; }

const ResolvedRule *
RuleDB::matchPatternRule(const std::string &Target) const {
  auto CacheIt = PatternMatchCache.find(Target);
  if (CacheIt != PatternMatchCache.end())
    return &CacheIt->second;

  for (auto &PR : PatternRules) {
    std::string Stem;
    if (PR.matchTarget(Target, Stem)) {
      ResolvedRule RR;
      RR.Target = Target;
      RR.Stem = Stem;
      RR.Recipes = PR.Recipes;
      RR.IsPhony = PhonyTargets.count(Target) > 0;

      bool PrereqsSatisfied = true;
      for (auto &PP : PR.PrereqPatterns) {
        std::string Prereq;
        size_t Pct = PP.find('%');
        if (Pct != std::string::npos)
          Prereq = PP.substr(0, Pct) + Stem + PP.substr(Pct + 1);
        else
          Prereq = PP;
        RR.Prerequisites.push_back(Prereq);

        // Only apply the pattern if each prerequisite either exists
        // on disk or has an explicit rule. This prevents infinite
        // recursion when a pattern like test_% matches its own
        // generated prerequisite test_%.c (which also starts with
        // the same prefix).
        if (!platform::fileExists(Prereq) &&
            ExplicitRules.find(Prereq) == ExplicitRules.end()) {
          PrereqsSatisfied = false;
          break;
        }
      }

      if (!PrereqsSatisfied)
        continue;

      auto Pair = PatternMatchCache.emplace(Target, std::move(RR));
      return &Pair.first->second;
    }
  }
  return nullptr;
}

} // namespace build
} // namespace neverc
