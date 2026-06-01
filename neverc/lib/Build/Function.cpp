#include "neverc/Build/Function.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/VariableEnv.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <sstream>

namespace neverc {
namespace build {

static std::string trim(const std::string &S) {
  size_t Start = S.find_first_not_of(" \t");
  if (Start == std::string::npos)
    return "";
  size_t End = S.find_last_not_of(" \t");
  return S.substr(Start, End - Start + 1);
}

static std::vector<std::string> splitWords(const std::string &S) {
  std::vector<std::string> Words;
  std::istringstream SS(S);
  std::string W;
  while (SS >> W)
    Words.push_back(W);
  return Words;
}

static std::string joinWords(const std::vector<std::string> &Words,
                              const std::string &Sep = " ") {
  std::string R;
  for (size_t I = 0; I < Words.size(); ++I) {
    if (I > 0)
      R += Sep;
    R += Words[I];
  }
  return R;
}

static bool matchPattern(const std::string &Pattern,
                          const std::string &Text) {
  size_t PctPos = Pattern.find('%');
  if (PctPos == std::string::npos)
    return Pattern == Text;
  std::string Prefix = Pattern.substr(0, PctPos);
  std::string Suffix = Pattern.substr(PctPos + 1);
  if (Text.size() < Prefix.size() + Suffix.size())
    return false;
  return Text.substr(0, Prefix.size()) == Prefix &&
         Text.substr(Text.size() - Suffix.size()) == Suffix;
}

static std::string stemFromPattern(const std::string &Pattern,
                                     const std::string &Text) {
  size_t PctPos = Pattern.find('%');
  if (PctPos == std::string::npos)
    return "";
  std::string Prefix = Pattern.substr(0, PctPos);
  std::string Suffix = Pattern.substr(PctPos + 1);
  return Text.substr(Prefix.size(),
                     Text.size() - Prefix.size() - Suffix.size());
}

FunctionRegistry::FunctionRegistry() { registerBuiltins(); }

std::vector<std::string> FunctionRegistry::splitArgs(const std::string &ArgStr,
                                                      unsigned MaxArgs) {
  std::vector<std::string> Args;
  int Depth = 0;
  size_t Start = 0;

  for (size_t I = 0; I < ArgStr.size(); ++I) {
    if (ArgStr[I] == '$' && I + 1 < ArgStr.size() &&
        (ArgStr[I + 1] == '(' || ArgStr[I + 1] == '{')) {
      ++Depth;
      ++I;
    } else if (Depth > 0 && (ArgStr[I] == ')' || ArgStr[I] == '}')) {
      --Depth;
    } else if (Depth == 0 && ArgStr[I] == ',') {
      if (MaxArgs > 0 && Args.size() + 1 >= MaxArgs) {
        break;
      }
      Args.push_back(ArgStr.substr(Start, I - Start));
      Start = I + 1;
    }
  }
  Args.push_back(ArgStr.substr(Start));
  return Args;
}

void FunctionRegistry::registerBuiltins() {
  // --- String functions ---

  registerFunction("subst", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.size() < 3)
      return "";
    std::string From = Args[0], To = Args[1], Text = Args[2];
    std::string Result;
    size_t Pos = 0;
    while (Pos < Text.size()) {
      size_t Found = Text.find(From, Pos);
      if (Found == std::string::npos || From.empty()) {
        Result += Text.substr(Pos);
        break;
      }
      Result += Text.substr(Pos, Found - Pos);
      Result += To;
      Pos = Found + From.size();
    }
    return Result;
  });

  registerFunction("patsubst", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    if (Args.size() < 3)
      return "";
    std::string Pattern = trim(Args[0]);
    std::string Replacement = trim(Args[1]);
    auto Words = splitWords(Args[2]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      if (matchPattern(Pattern, W)) {
        std::string Stem = stemFromPattern(Pattern, W);
        size_t Pct = Replacement.find('%');
        if (Pct != std::string::npos)
          Result.push_back(Replacement.substr(0, Pct) + Stem +
                           Replacement.substr(Pct + 1));
        else
          Result.push_back(Replacement);
      } else {
        Result.push_back(W);
      }
    }
    return joinWords(Result);
  });

  registerFunction("strip", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    return joinWords(splitWords(Args[0]));
  });

  registerFunction("findstring", [](const std::vector<std::string> &Args,
                                     VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    return (Args[1].find(Args[0]) != std::string::npos) ? Args[0] : "";
  });

  registerFunction("filter", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    auto Patterns = splitWords(Args[0]);
    auto Words = splitWords(Args[1]);
    std::vector<std::string> Result;
    for (auto &W : Words)
      for (auto &P : Patterns)
        if (matchPattern(P, W)) {
          Result.push_back(W);
          break;
        }
    return joinWords(Result);
  });

  registerFunction("filter-out", [](const std::vector<std::string> &Args,
                                     VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    auto Patterns = splitWords(Args[0]);
    auto Words = splitWords(Args[1]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      bool Matched = false;
      for (auto &P : Patterns)
        if (matchPattern(P, W)) {
          Matched = true;
          break;
        }
      if (!Matched)
        Result.push_back(W);
    }
    return joinWords(Result);
  });

  registerFunction("sort", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::set<std::string> Sorted(Words.begin(), Words.end());
    return joinWords(std::vector<std::string>(Sorted.begin(), Sorted.end()));
  });

  registerFunction("word", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    int N = std::atoi(trim(Args[0]).c_str());
    auto Words = splitWords(Args[1]);
    if (N < 1 || (size_t)N > Words.size())
      return "";
    return Words[N - 1];
  });

  registerFunction("wordlist", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    if (Args.size() < 3)
      return "";
    int S = std::atoi(trim(Args[0]).c_str());
    int E = std::atoi(trim(Args[1]).c_str());
    auto Words = splitWords(Args[2]);
    if (S < 1)
      S = 1;
    if ((size_t)E > Words.size())
      E = Words.size();
    std::vector<std::string> Result;
    for (int I = S; I <= E; ++I)
      Result.push_back(Words[I - 1]);
    return joinWords(Result);
  });

  registerFunction("words", [](const std::vector<std::string> &Args,
                                 VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "0";
    return std::to_string(splitWords(Args[0]).size());
  });

  registerFunction("firstword", [](const std::vector<std::string> &Args,
                                    VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    return Words.empty() ? "" : Words[0];
  });

  registerFunction("lastword", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    return Words.empty() ? "" : Words.back();
  });

  // --- Filename functions ---

  registerFunction("dir", [](const std::vector<std::string> &Args,
                              VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      size_t Slash = W.find_last_of("/\\");
      Result.push_back(Slash == std::string::npos ? "./"
                                                   : W.substr(0, Slash + 1));
    }
    return joinWords(Result);
  });

  registerFunction("notdir", [](const std::vector<std::string> &Args,
                                 VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      size_t Slash = W.find_last_of("/\\");
      Result.push_back(Slash == std::string::npos ? W
                                                   : W.substr(Slash + 1));
    }
    return joinWords(Result);
  });

  registerFunction("suffix", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      size_t Dot = W.rfind('.');
      size_t Slash = W.find_last_of("/\\");
      if (Dot != std::string::npos &&
          (Slash == std::string::npos || Dot > Slash))
        Result.push_back(W.substr(Dot));
    }
    return joinWords(Result);
  });

  registerFunction("basename", [](const std::vector<std::string> &Args,
                                    VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      size_t Dot = W.rfind('.');
      size_t Slash = W.find_last_of("/\\");
      if (Dot != std::string::npos &&
          (Slash == std::string::npos || Dot > Slash))
        Result.push_back(W.substr(0, Dot));
      else
        Result.push_back(W);
    }
    return joinWords(Result);
  });

  registerFunction("addsuffix", [](const std::vector<std::string> &Args,
                                    VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    std::string Suffix = trim(Args[0]);
    auto Words = splitWords(Args[1]);
    std::vector<std::string> Result;
    for (auto &W : Words)
      Result.push_back(W + Suffix);
    return joinWords(Result);
  });

  registerFunction("addprefix", [](const std::vector<std::string> &Args,
                                    VariableEnv &Env) -> std::string {
    if (Args.size() < 2)
      return "";
    std::string Prefix = trim(Args[0]);
    auto Words = splitWords(Args[1]);
    std::vector<std::string> Result;
    for (auto &W : Words)
      Result.push_back(Prefix + W);
    return joinWords(Result);
  });

  registerFunction("wildcard", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Patterns = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &P : Patterns) {
      auto Files = platform::globFiles(P);
      Result.insert(Result.end(), Files.begin(), Files.end());
    }
    return joinWords(Result);
  });

  registerFunction("abspath", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words)
      Result.push_back(platform::realPath(W));
    return joinWords(Result);
  });

  // --- Conditional functions ---

  registerFunction("if", [](const std::vector<std::string> &Args,
                              VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    std::string Cond = trim(Env.expand(Args[0]));
    if (!Cond.empty())
      return Args.size() > 1 ? Env.expand(Args[1]) : "";
    return Args.size() > 2 ? Env.expand(Args[2]) : "";
  });

  registerFunction("or", [](const std::vector<std::string> &Args,
                              VariableEnv &Env) -> std::string {
    for (auto &A : Args) {
      std::string V = trim(Env.expand(A));
      if (!V.empty())
        return V;
    }
    return "";
  });

  registerFunction("and", [](const std::vector<std::string> &Args,
                               VariableEnv &Env) -> std::string {
    std::string Last;
    for (auto &A : Args) {
      std::string V = trim(Env.expand(A));
      if (V.empty())
        return "";
      Last = V;
    }
    return Last;
  });

  // --- Iteration / control ---

  registerFunction("foreach", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.size() < 3)
      return "";
    std::string VarName = trim(Env.expand(Args[0]));
    auto Words = splitWords(Env.expand(Args[1]));
    std::string Template = Args[2]; // kept raw for per-iteration expansion
    std::vector<std::string> Result;
    for (auto &W : Words) {
      Env.set(VarName, W, AssignMode::Simple);
      Result.push_back(Env.expand(Template));
    }
    return joinWords(Result);
  });

  registerFunction("call", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    std::string VarName = trim(Env.expand(Args[0]));
    std::string Body = Env.rawValue(VarName);

    // Save ALL existing positional variables (1-9), including undefined state.
    struct SavedVar {
      std::string Key;
      std::string Value;
      bool WasDefined;
    };
    std::vector<SavedVar> Saved;
    for (size_t I = 1; I < 10; ++I) {
      std::string Key = std::to_string(I);
      bool Defined = Env.isDefined(Key);
      Saved.push_back({Key, Defined ? Env.rawValue(Key) : "", Defined});
    }

    for (size_t I = 1; I < Args.size(); ++I) {
      Env.set(std::to_string(I), trim(Env.expand(Args[I])),
              AssignMode::Simple);
    }
    for (size_t I = Args.size(); I < 10; ++I) {
      std::string Key = std::to_string(I);
      if (Env.isDefined(Key))
        Env.set(Key, "", AssignMode::Simple);
    }

    std::string Result = Env.expand(Body);

    // Restore positional variables to their saved state.
    for (auto &S : Saved) {
      if (S.WasDefined)
        Env.set(S.Key, S.Value, AssignMode::Simple);
      else
        Env.set(S.Key, "", AssignMode::Simple);
    }

    return Result;
  });

  registerFunction("eval", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (!Args.empty()) {
      std::string Text = Env.expand(Args[0]);
      Env.invokeEval(Text);
    }
    return "";
  });

  // --- Info / debug ---

  registerFunction("error", [](const std::vector<std::string> &Args,
                                 VariableEnv &Env) -> std::string {
    std::string Msg = Args.empty() ? "" : Args[0];
    std::fprintf(stderr, "*** %s.  Stop.\n", Msg.c_str());
    std::exit(2);
    return "";
  });

  registerFunction("warning", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    std::string Msg = Args.empty() ? "" : Args[0];
    std::fprintf(stderr, "warning: %s\n", Msg.c_str());
    return "";
  });

  registerFunction("info", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    std::string Msg = Args.empty() ? "" : Args[0];
    std::fprintf(stdout, "%s\n", Msg.c_str());
    return "";
  });

  // --- Shell ---

  registerFunction("shell", [](const std::vector<std::string> &Args,
                                 VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto R = platform::shellExecute(Args[0]);
    std::string Out = R.Output;
    std::replace(Out.begin(), Out.end(), '\n', ' ');
    return Out;
  });
}

void FunctionRegistry::registerFunction(const std::string &Name,
                                         FuncImpl Impl) {
  Registry[Name] = std::move(Impl);
}

std::string FunctionRegistry::call(const std::string &Name,
                                    const std::vector<std::string> &Args,
                                    VariableEnv &Env) const {
  auto It = Registry.find(Name);
  if (It == Registry.end())
    return "";
  return It->second(Args, Env);
}

bool FunctionRegistry::hasFunction(const std::string &Name) const {
  return Registry.count(Name) > 0;
}

} // namespace build
} // namespace neverc
