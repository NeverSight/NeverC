#include "neverc/Build/Function.h"
#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/StringUtils.h"
#include "neverc/Build/VariableEnv.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace neverc {
namespace build {

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
      Result.push_back(platform::absolutePath(W));
    return joinWords(Result);
  });

  registerFunction("realpath", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    auto Words = splitWords(Args[0]);
    std::vector<std::string> Result;
    for (auto &W : Words) {
      if (!platform::fileExists(W))
        continue;
      Result.push_back(platform::realPath(W));
    }
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

    bool WasDefined = false;
    VariableEnv::Variable SavedVar;
    {
      auto It = Env.vars().find(VarName);
      if (It != Env.vars().end()) {
        WasDefined = true;
        SavedVar = It->second;
      }
    }

    std::vector<std::string> Result;
    for (auto &W : Words) {
      Env.setForced(VarName, W, AssignMode::Simple);
      Result.push_back(Env.expand(Template));
    }

    if (WasDefined)
      Env.setForced(VarName, SavedVar.Value, SavedVar.Mode, SavedVar.Orig);
    else
      Env.undefine(VarName);

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
    for (size_t I = 1; I <= constants::MaxPositionalArgs; ++I) {
      std::string Key = std::to_string(I);
      bool Defined = Env.isDefined(Key);
      Saved.push_back({Key, Defined ? Env.rawValue(Key) : "", Defined});
    }

    std::vector<std::string> ExpandedArgs;
    for (size_t I = 1; I < Args.size(); ++I)
      ExpandedArgs.push_back(trim(Env.expand(Args[I])));
    for (size_t I = 0; I < ExpandedArgs.size(); ++I)
      Env.setForced(std::to_string(I + 1), ExpandedArgs[I],
                    AssignMode::Simple);
    for (size_t I = Args.size(); I <= constants::MaxPositionalArgs; ++I) {
      std::string Key = std::to_string(I);
      if (Env.isDefined(Key))
        Env.setForced(Key, "", AssignMode::Simple);
    }

    std::string Result = Env.expand(Body);

    for (auto &S : Saved) {
      if (S.WasDefined)
        Env.setForced(S.Key, S.Value, AssignMode::Simple);
      else
        Env.setForced(S.Key, "", AssignMode::Simple);
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
    llvm::errs() << constants::ErrorPrefix << Msg << ".  Stop.\n";
    std::exit(2);
    return "";
  });

  registerFunction("warning", [](const std::vector<std::string> &Args,
                                   VariableEnv &Env) -> std::string {
    std::string Msg = Args.empty() ? "" : Args[0];
    llvm::errs() << "warning: " << Msg << "\n";
    return "";
  });

  registerFunction("info", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    std::string Msg = Args.empty() ? "" : Args[0];
    llvm::outs() << Msg << "\n";
    return "";
  });

  // --- Variable introspection ---

  registerFunction("origin", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "undefined";
    std::string Name = trim(Args[0]);
    if (!Env.isDefined(Name))
      return "undefined";
    switch (Env.getOrigin(Name)) {
    case VariableEnv::Origin::Default:
      return "default";
    case VariableEnv::Origin::Environment:
      return "environment";
    case VariableEnv::Origin::File:
      return "file";
    case VariableEnv::Origin::CommandLine:
      return "command line";
    case VariableEnv::Origin::Override:
      return "override";
    case VariableEnv::Origin::Automatic:
      return "automatic";
    }
    return "undefined";
  });

  registerFunction("value", [](const std::vector<std::string> &Args,
                                 VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    return Env.rawValue(trim(Args[0]));
  });

  registerFunction("flavor", [](const std::vector<std::string> &Args,
                                  VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "undefined";
    std::string Name = trim(Args[0]);
    if (!Env.isDefined(Name))
      return "undefined";
    return Env.getFlavor(Name);
  });

  // --- File ---

  registerFunction("file", [](const std::vector<std::string> &Args,
                                VariableEnv &Env) -> std::string {
    if (Args.empty())
      return "";
    std::string Op = trim(Args[0]);
    if (Op.size() < 2)
      return "";
    char Mode = Op[0];
    bool Append = (Mode == '>' && Op.size() >= 2 && Op[1] == '>');
    std::string Filename = trim(Append ? Op.substr(2) : Op.substr(1));
    if (Filename.empty())
      return "";

    if (Mode == '<') {
      std::string Path = trim(Op.substr(1));
      std::ifstream In(Path);
      if (!In.is_open())
        return "";
      std::string Content((std::istreambuf_iterator<char>(In)),
                           std::istreambuf_iterator<char>());
      if (!Content.empty() && Content.back() == '\n')
        Content.pop_back();
      return Content;
    }

    if (Mode == '>') {
      bool HasText = Args.size() > 1;
      std::string Text = HasText ? Args[1] : "";
      std::ofstream Out(Filename,
                        Append ? (std::ios::app | std::ios::out) : std::ios::out);
      if (!Out.is_open()) {
        llvm::errs() << constants::ErrorPrefix << "open: " << Filename
                     << ": No such file or directory\n";
        return "";
      }
      if (HasText)
        Out << Text << "\n";
      return "";
    }

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
