#include "neverc/Build/VariableEnv.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/Platform.h"

#include <cstdlib>
#include <sstream>

extern "C" char **environ;

namespace neverc {
namespace build {

VariableEnv::VariableEnv() {
  set("CURDIR", platform::getCwd(), AssignMode::Simple, Origin::Default);
  set("MAKE", "neverc make", AssignMode::Simple, Origin::Default);
  set("SHELL", platform::getDefaultShell(), AssignMode::Simple,
      Origin::Default);
  set(".SHELLFLAGS", "-c", AssignMode::Simple, Origin::Default);
}

void VariableEnv::set(const std::string &Name, const std::string &Value,
                      AssignMode Mode, Origin Orig) {
  auto It = Vars.find(Name);
  if (It != Vars.end()) {
    if (Orig == Origin::File && It->second.Orig == Origin::CommandLine)
      return;
    bool WasExported = It->second.Exported;
    Vars[Name] = {Value, Mode, Orig, WasExported};
  } else {
    bool Exported = ExportAllFlag || PendingExports.count(Name) > 0;
    Vars[Name] = {Value, Mode, Orig, Exported};
    if (PendingExports.count(Name))
      PendingExports.erase(Name);
  }
}

void VariableEnv::setForced(const std::string &Name, const std::string &Value,
                            AssignMode Mode, Origin Orig) {
  bool WasExported = ExportAllFlag;
  auto It = Vars.find(Name);
  if (It != Vars.end()) {
    WasExported = It->second.Exported;
  } else if (PendingExports.count(Name)) {
    WasExported = true;
    PendingExports.erase(Name);
  }
  Vars[Name] = {Value, Mode, Orig, WasExported};
}

void VariableEnv::append(const std::string &Name, const std::string &Value) {
  auto It = Vars.find(Name);
  if (It == Vars.end()) {
    set(Name, Value, AssignMode::Recursive);
  } else {
    if (It->second.Value.empty())
      It->second.Value = Value;
    else
      It->second.Value += " " + Value;
  }
}

void VariableEnv::conditionalSet(const std::string &Name,
                                  const std::string &Value) {
  if (!isDefined(Name))
    set(Name, Value, AssignMode::Recursive);
}

void VariableEnv::setExport(const std::string &Name, bool Export) {
  auto It = Vars.find(Name);
  if (It != Vars.end()) {
    It->second.Exported = Export;
    if (!Export)
      PendingExports.erase(Name);
  } else if (Export) {
    PendingExports.insert(Name);
  } else {
    PendingExports.erase(Name);
  }
}

void VariableEnv::undefine(const std::string &Name) { Vars.erase(Name); }

std::string VariableEnv::get(const std::string &Name) {
  auto AIt = AutoVars.find(Name);
  if (AIt != AutoVars.end())
    return AIt->second;

  auto It = Vars.find(Name);
  if (It == Vars.end())
    return "";

  if (It->second.Mode == AssignMode::Recursive) {
    std::unordered_set<std::string> Expanding;
    return expandInternal(It->second.Value, Expanding);
  }
  return It->second.Value;
}

std::string VariableEnv::expand(const std::string &Expr) {
  std::unordered_set<std::string> Expanding;
  return expandInternal(Expr, Expanding);
}

bool VariableEnv::isDefined(const std::string &Name) const {
  return AutoVars.count(Name) || Vars.count(Name);
}

std::string VariableEnv::rawValue(const std::string &Name) const {
  auto It = Vars.find(Name);
  if (It == Vars.end())
    return "";
  return It->second.Value;
}

VariableEnv::Origin VariableEnv::getOrigin(const std::string &Name) const {
  if (AutoVars.count(Name))
    return Origin::Automatic;
  auto It = Vars.find(Name);
  if (It == Vars.end())
    return Origin::Default;
  return It->second.Orig;
}

std::string VariableEnv::getFlavor(const std::string &Name) const {
  auto It = Vars.find(Name);
  if (It == Vars.end())
    return "undefined";
  switch (It->second.Mode) {
  case AssignMode::Recursive:
    return "recursive";
  case AssignMode::Simple:
    return "simple";
  case AssignMode::Conditional:
    return "recursive";
  case AssignMode::Append:
    return "recursive";
  case AssignMode::Shell:
    return "simple";
  }
  return "undefined";
}

void VariableEnv::setAutoVar(const std::string &Name,
                              const std::string &Value) {
  AutoVars[Name] = Value;
}

void VariableEnv::clearAutoVars() { AutoVars.clear(); }

void VariableEnv::importEnvironment() {
  if (!::environ)
    return;
  for (char **Env = ::environ; *Env; ++Env) {
    std::string Entry = *Env;
    size_t Eq = Entry.find('=');
    if (Eq == std::string::npos)
      continue;
    std::string Name = Entry.substr(0, Eq);
    std::string Value = Entry.substr(Eq + 1);
    if (!isDefined(Name))
      set(Name, Value, AssignMode::Simple, Origin::Environment);
  }
}

void VariableEnv::setCommandLineVar(const std::string &Name,
                                     const std::string &Value) {
  bool WasExported = false;
  auto It = Vars.find(Name);
  if (It != Vars.end())
    WasExported = It->second.Exported;
  Vars[Name] = {Value, AssignMode::Simple, Origin::CommandLine, WasExported};
}

std::string
VariableEnv::expandInternal(const std::string &Expr,
                             std::unordered_set<std::string> &Expanding) {
  if (++RecursionDepth > MaxRecursionDepth) {
    --RecursionDepth;
    std::fprintf(stderr,
                 "neverc make: *** Recursion depth exceeded (max %u). Stop.\n",
                 MaxRecursionDepth);
    return "";
  }
  struct DepthGuard {
    unsigned &Depth;
    ~DepthGuard() { --Depth; }
  } Guard{RecursionDepth};

  std::string Result;
  Result.reserve(Expr.size());

  for (size_t I = 0; I < Expr.size(); ++I) {
    if (Expr[I] == '$') {
      if (I + 1 >= Expr.size()) {
        Result += '$';
        continue;
      }
      if (Expr[I + 1] == '$') {
        Result += '$';
        ++I;
        continue;
      }
      ++I;
      Result += expandVarRef(Expr, I, Expanding);
    } else {
      Result += Expr[I];
    }
  }
  return Result;
}

std::string
VariableEnv::expandVarRef(const std::string &Expr, size_t &Pos,
                           std::unordered_set<std::string> &Expanding) {
  if (Pos >= Expr.size())
    return "";

  char C = Expr[Pos];

  if (C == '(' || C == '{') {
    char Close = (C == '(') ? ')' : '}';
    size_t Start = Pos + 1;
    int Depth = 1;
    size_t End = Start;
    while (End < Expr.size() && Depth > 0) {
      if (Expr[End] == C)
        ++Depth;
      else if (Expr[End] == Close)
        --Depth;
      if (Depth > 0)
        ++End;
    }
    std::string RawInner = Expr.substr(Start, End - Start);
    Pos = End;

    // Check for function call BEFORE expanding: look for first space
    // at depth 0 in the raw inner string to identify function name.
    if (FuncReg) {
      size_t FuncSpacePos = std::string::npos;
      int FuncDepth = 0;
      for (size_t I = 0; I < RawInner.size(); ++I) {
        if (RawInner[I] == '$' && I + 1 < RawInner.size() &&
            (RawInner[I + 1] == '(' || RawInner[I + 1] == '{')) {
          ++FuncDepth;
          ++I;
        } else if (FuncDepth > 0 &&
                   (RawInner[I] == ')' || RawInner[I] == '}')) {
          --FuncDepth;
        } else if (FuncDepth == 0 && RawInner[I] == ' ') {
          FuncSpacePos = I;
          break;
        }
      }
      if (FuncSpacePos != std::string::npos) {
        std::string FuncName = RawInner.substr(0, FuncSpacePos);
        if (FuncReg->hasFunction(FuncName)) {
          std::string RawArgs = RawInner.substr(FuncSpacePos + 1);
          return evaluateFunction(FuncName, RawArgs, Expanding);
        }
      }
    }

    // Check for substitution reference $(VAR:pattern=replacement)
    // before expanding the inner content as a variable name.
    {
      size_t ColonPos = std::string::npos;
      int SubDepth = 0;
      for (size_t I = 0; I < RawInner.size(); ++I) {
        if (RawInner[I] == '$' && I + 1 < RawInner.size() &&
            (RawInner[I + 1] == '(' || RawInner[I + 1] == '{')) {
          ++SubDepth;
          ++I;
        } else if (SubDepth > 0 &&
                   (RawInner[I] == ')' || RawInner[I] == '}')) {
          --SubDepth;
        } else if (SubDepth == 0 && RawInner[I] == ':') {
          ColonPos = I;
          break;
        }
      }
      if (ColonPos != std::string::npos) {
        std::string SubRest = RawInner.substr(ColonPos + 1);
        size_t EqPos = SubRest.find('=');
        if (EqPos != std::string::npos) {
          std::string VarName =
              expandInternal(RawInner.substr(0, ColonPos), Expanding);
          std::string RawPat = SubRest.substr(0, EqPos);
          std::string RawRep = SubRest.substr(EqPos + 1);
          std::string Pattern =
              (RawPat.find('%') != std::string::npos) ? RawPat
                                                      : "%" + RawPat;
          std::string Replacement =
              (RawRep.find('%') != std::string::npos) ? RawRep
                                                      : "%" + RawRep;

          if (Expanding.count(VarName))
            return "";
          Expanding.insert(VarName);

          std::string VarVal;
          auto SubAIt = AutoVars.find(VarName);
          if (SubAIt != AutoVars.end()) {
            VarVal = SubAIt->second;
          } else {
            auto SubIt = Vars.find(VarName);
            if (SubIt != Vars.end()) {
              if (SubIt->second.Mode == AssignMode::Recursive)
                VarVal = expandInternal(SubIt->second.Value, Expanding);
              else
                VarVal = SubIt->second.Value;
            }
          }
          Expanding.erase(VarName);

          // Apply patsubst-style substitution
          std::istringstream SS(VarVal);
          std::string Word, Result;
          bool First = true;
          while (SS >> Word) {
            if (!First)
              Result += ' ';
            First = false;
            size_t Pct = Pattern.find('%');
            std::string Prefix = Pattern.substr(0, Pct);
            std::string Suffix = Pattern.substr(Pct + 1);
            if (Word.size() >= Prefix.size() + Suffix.size() &&
                Word.substr(0, Prefix.size()) == Prefix &&
                (Suffix.empty() ||
                 Word.substr(Word.size() - Suffix.size()) == Suffix)) {
              std::string Stem =
                  Word.substr(Prefix.size(),
                              Word.size() - Prefix.size() - Suffix.size());
              size_t RPct = Replacement.find('%');
              if (RPct != std::string::npos)
                Result += Replacement.substr(0, RPct) + Stem +
                          Replacement.substr(RPct + 1);
              else
                Result += Replacement;
            } else {
              Result += Word;
            }
          }
          return Result;
        }
      }
    }

    std::string Inner = expandInternal(RawInner, Expanding);

    if (Expanding.count(Inner))
      return "";
    Expanding.insert(Inner);

    auto AIt = AutoVars.find(Inner);
    if (AIt != AutoVars.end()) {
      Expanding.erase(Inner);
      return AIt->second;
    }

    auto It = Vars.find(Inner);
    if (It == Vars.end()) {
      Expanding.erase(Inner);
      return "";
    }

    std::string Val;
    if (It->second.Mode == AssignMode::Recursive)
      Val = expandInternal(It->second.Value, Expanding);
    else
      Val = It->second.Value;

    Expanding.erase(Inner);
    return Val;
  }

  // Single-char variable: $X
  std::string Name(1, C);
  auto AIt = AutoVars.find(Name);
  if (AIt != AutoVars.end())
    return AIt->second;
  auto It = Vars.find(Name);
  if (It != Vars.end()) {
    if (It->second.Mode == AssignMode::Recursive)
      return expandInternal(It->second.Value, Expanding);
    return It->second.Value;
  }
  return "";
}

std::string VariableEnv::evaluateFunction(
    const std::string &Name, const std::string &RawArgs,
    std::unordered_set<std::string> &Expanding) {
  if (!FuncReg)
    return "";

  static const std::unordered_set<std::string> LazyFuncs = {
      "foreach", "call", "if", "or", "and", "eval"};

  auto ArgList = FunctionRegistry::splitArgs(RawArgs);

  if (LazyFuncs.count(Name))
    return FuncReg->call(Name, ArgList, *this);

  std::vector<std::string> ExpandedArgs;
  ExpandedArgs.reserve(ArgList.size());
  for (auto &A : ArgList)
    ExpandedArgs.push_back(expandInternal(A, Expanding));
  return FuncReg->call(Name, ExpandedArgs, *this);
}

} // namespace build
} // namespace neverc
