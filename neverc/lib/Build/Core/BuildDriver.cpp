#include "neverc/Build/BuildDriver.h"
#include "neverc/Build/AST.h"
#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/DepGraph.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/JobScheduler.h"
#include "neverc/Build/Lexer.h"
#include "neverc/Build/MakefileInterpreter.h"
#include "neverc/Build/Parser.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/RecursiveMakeEnvironment.h"
#include "neverc/Build/RuleDB.h"
#include "neverc/Build/VariableEnv.h"

#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <string>
#include <vector>

namespace neverc {
namespace build {

namespace {

struct BuildOptions {
  std::string MakefilePath;
  std::vector<std::string> ChangeDirs;
  unsigned Jobs = 1;
  bool DryRun = false;
  bool KeepGoing = false;
  bool Silent = false;
  bool AlwaysMake = false;
  bool PrintDataBase = false;
  std::vector<std::string> Targets;
  std::vector<std::pair<std::string, std::string>> CmdVars;
};

enum class ParseOptionsResult {
  Continue,
  ExitSuccess,
  ExitFailure,
};

ParseOptionsResult parseOptions(int Argc, const char **Argv,
                                BuildOptions &Opts) {
  int I = 1;
  while (I < Argc) {
    std::string Arg = Argv[I];

    if (Arg == "-f" && I + 1 < Argc) {
      Opts.MakefilePath = Argv[++I];
    } else if (Arg == "-j") {
      if (I + 1 < Argc) {
        std::string V = Argv[I + 1];
        if (!V.empty() && V[0] != '-') {
          Opts.Jobs = std::atoi(V.c_str());
          ++I;
        } else {
          Opts.Jobs = platform::getProcessorCount();
        }
      } else {
        Opts.Jobs = platform::getProcessorCount();
      }
    } else if (Arg.substr(0, 2) == "-j" && Arg.size() > 2) {
      Opts.Jobs = std::atoi(Arg.substr(2).c_str());
    } else if (Arg == "-C" && I + 1 < Argc) {
      Opts.ChangeDirs.push_back(Argv[++I]);
    } else if (Arg == "-n" || Arg == "--dry-run" || Arg == "--just-print" ||
               Arg == "--recon") {
      Opts.DryRun = true;
    } else if (Arg == "-k" || Arg == "--keep-going") {
      Opts.KeepGoing = true;
    } else if (Arg == "-s" || Arg == "--silent" || Arg == "--quiet") {
      Opts.Silent = true;
    } else if (Arg == "-B" || Arg == "--always-make") {
      Opts.AlwaysMake = true;
    } else if (Arg == "-p" || Arg == "--print-data-base") {
      Opts.PrintDataBase = true;
    } else if (Arg == "-h" || Arg == "--help") {
      llvm::outs()
          << "Usage: neverc build [options] [target...]\n"
          << "       neverc make  [options] [target...]\n"
          << "\n"
          << "Options:\n"
          << "  -f FILE          Read FILE as Makefile\n"
          << "  -j [N]           Allow N parallel jobs (default: CPU count)\n"
          << "  -C DIR           Change directory before reading Makefile\n"
          << "  -n, --dry-run    Print commands without executing\n"
          << "  -k, --keep-going Continue after errors\n"
          << "  -s, --silent     Don't echo commands\n"
          << "  -B, --always-make Unconditionally rebuild all targets\n"
          << "  -p               Print rule database\n"
          << "  VAR=VALUE        Set variable\n"
          << "  -h, --help       Show this help\n";
      return ParseOptionsResult::ExitSuccess;
    } else if (Arg.find('=') != std::string::npos) {
      size_t Eq = Arg.find('=');
      Opts.CmdVars.emplace_back(Arg.substr(0, Eq), Arg.substr(Eq + 1));
    } else if (!Arg.empty() && Arg[0] != '-') {
      Opts.Targets.push_back(Arg);
    } else {
      llvm::errs() << constants::ToolName << ": Unknown option: "
                   << Arg << "\n";
      return ParseOptionsResult::ExitFailure;
    }
    ++I;
  }
  if (Opts.Jobs == 0)
    Opts.Jobs = 1;
  return ParseOptionsResult::Continue;
}

std::string findMakefile(const std::string &Specified) {
  if (!Specified.empty()) {
    if (platform::fileExists(Specified))
      return Specified;
    llvm::errs() << constants::ToolName << ": " << Specified
                 << ": No such file or directory\n";
    return "";
  }

  for (const auto &Name : constants::DefaultMakefiles)
    if (platform::fileExists(Name.str()))
      return Name.str();

  llvm::errs() << constants::ErrorPrefix
               << "No targets specified and no makefile found. Stop.\n";
  return "";
}

void printDatabase(const RuleDB &Rules, const VariableEnv &Env) {
  llvm::outs() << "# Variables\n";
  for (auto &Entry : Env.vars())
    llvm::outs() << Entry.first() << " = " << Entry.second.Value << "\n";
  llvm::outs() << "\n# Rules\n";
  for (auto &RuleEntry : Rules.rules()) {
    for (auto &R : RuleEntry.second) {
      llvm::outs() << R.Target << ":";
      for (auto &P : R.Prerequisites)
        llvm::outs() << " " << P;
      llvm::outs() << "\n";
      for (auto &Rec : R.Recipes) {
        llvm::outs() << "\t";
        if (Rec.Silent) llvm::outs() << "@";
        if (Rec.IgnoreError) llvm::outs() << "-";
        if (Rec.Force) llvm::outs() << "+";
        llvm::outs() << Rec.Command << "\n";
      }
    }
  }
}

} // namespace

int runBuild(int Argc, const char **Argv, const char *Argv0,
             const char *PrependArg) {
  BuildOptions Opts;
  const ParseOptionsResult ParseResult = parseOptions(Argc, Argv, Opts);
  if (ParseResult == ParseOptionsResult::ExitSuccess)
    return 0;
  if (ParseResult == ParseOptionsResult::ExitFailure)
    return 2;

  static int ExecutableAnchor;
  std::string MakeExecutable = llvm::sys::fs::getMainExecutable(
      Argv0, static_cast<void *>(&ExecutableAnchor));
  if (MakeExecutable.empty())
    MakeExecutable = platform::absolutePath(Argv0);

  for (auto &Dir : Opts.ChangeDirs) {
    if (!platform::changeCwd(Dir)) {
      llvm::errs() << constants::ErrorPrefix << "No such directory: "
                   << Dir << "\n";
      return 2;
    }
  }

  std::string MakefilePath = findMakefile(Opts.MakefilePath);
  if (MakefilePath.empty())
    return 2;

  auto Buf = llvm::MemoryBuffer::getFile(MakefilePath);
  if (!Buf) {
    llvm::errs() << constants::ErrorPrefix << "Cannot read "
                 << MakefilePath << "\n";
    return 2;
  }

  std::string Content = (*Buf)->getBuffer().str();

  VariableEnv Env;
  FunctionRegistry FuncReg;
  RuleDB Rules;
  Env.setFunctionRegistry(&FuncReg);
  Env.importEnvironment();

  MakefileInterpreter Interpreter(Env, Rules, FuncReg);
  Env.setEvalCallback([&Interpreter](const std::string &Text) {
    std::string Filename = constants::EvalFilename.str();
    Lexer EvalL(Filename, Text);
    auto EvalLines = EvalL.lex();
    Parser EvalP(Filename, std::move(EvalLines));
    auto EvalAST = EvalP.parse();
    if (EvalAST)
      Interpreter.process(*EvalAST);
  });

  RecursiveMakeEnvironmentConfig EnvironmentConfig;
  EnvironmentConfig.MakefilePath = MakefilePath;
  EnvironmentConfig.MakeExecutable = MakeExecutable;
  const std::string CurrentDirectory = platform::getCwd();
  EnvironmentConfig.CurrentDirectory = CurrentDirectory;
  if (PrependArg)
    EnvironmentConfig.PrependArg = PrependArg;
  EnvironmentConfig.CommandLineVariables = Opts.CmdVars;
  EnvironmentConfig.Targets = Opts.Targets;
  EnvironmentConfig.Jobs = Opts.Jobs;
  EnvironmentConfig.DryRun = Opts.DryRun;
  EnvironmentConfig.KeepGoing = Opts.KeepGoing;
  EnvironmentConfig.Silent = Opts.Silent;
  EnvironmentConfig.AlwaysMake = Opts.AlwaysMake;
  RecursiveMakeEnvironment::apply(Env, EnvironmentConfig);

  Lexer L(MakefilePath, Content);
  auto Lines = L.lex();
  if (L.hadError()) {
    llvm::errs() << L.errorMessage() << "\n";
    return 2;
  }

  Parser P(MakefilePath, std::move(Lines));
  auto AST = P.parse();
  if (P.hadError()) {
    llvm::errs() << P.errorMessage() << "\n";
    return 2;
  }

  Interpreter.process(*AST);

  if (Opts.PrintDataBase) {
    printDatabase(Rules, Env);
    return 0;
  }

  std::vector<std::string> Targets = Opts.Targets;
  if (Targets.empty()) {
    std::string Default;
    std::string DGVar = Env.get(constants::TargetDefaultGoal.str());
    if (!DGVar.empty())
      Default = Env.expand(DGVar);
    if (Default.empty())
      Default = Rules.defaultTarget();
    if (Default.empty()) {
      llvm::errs() << constants::ErrorPrefix << "No targets. Stop.\n";
      return 2;
    }
    Targets.push_back(Default);
  }

  DepGraph Graph;
  for (auto &T : Targets) {
    if (!Graph.build(T, Rules, Opts.AlwaysMake)) {
      if (Graph.hasCycle())
        llvm::errs() << constants::ToolName << ": "
                     << Graph.cycleMessage() << "\n";
      return 2;
    }
  }

  bool HasMissingInput = false;
  {
    llvm::StringSet<> AllPrereqs;
    for (auto &Entry : Graph.nodes()) {
      for (auto &Dep : Entry.second.Dependencies)
        AllPrereqs.insert(Dep);
      for (auto &Dep : Entry.second.OrderOnlyDeps)
        AllPrereqs.insert(Dep);
    }
    llvm::StringSet<> RequestedTargets;
    for (const std::string &Target : Targets)
      RequestedTargets.insert(Target);

    for (auto &Entry : Graph.nodes()) {
      if (Entry.second.Rule || Entry.second.IsPhony)
        continue;
      if (platform::fileExists(Entry.first().str()))
        continue;
      if (AllPrereqs.count(Entry.first()) ||
          RequestedTargets.count(Entry.first())) {
        llvm::errs() << constants::ErrorPrefix << "No rule to make target '"
                     << Entry.first() << "'.  Stop.\n";
        Entry.second.Failed = true;
        HasMissingInput = true;
        if (!Opts.KeepGoing)
          return 2;
      }
    }
  }

  bool NothingToDo = true;
  for (auto &T : Targets) {
    auto *N = Graph.getNode(T);
    if (!N)
      continue;
    if (N->Failed) {
      NothingToDo = false;
      break;
    }
    if (N->IsPhony) {
      bool HasRecipes = N->Rule && !N->Rule->Recipes.empty();
      if (HasRecipes) {
        NothingToDo = false;
      } else {
        for (auto &Dep : N->Dependencies) {
          auto *DN = Graph.getNode(Dep);
          if (DN && DN->NeedsBuild) {
            NothingToDo = false;
            break;
          }
        }
      }
    } else if (N->NeedsBuild) {
      NothingToDo = false;
    }
    if (!NothingToDo)
      break;
  }
  if (NothingToDo && !Opts.AlwaysMake && !HasMissingInput) {
    for (auto &T : Targets) {
      auto *N = Graph.getNode(T);
      if (N && N->IsPhony)
        llvm::outs() << constants::ToolName
                     << ": Nothing to be done for '" << T << "'.\n";
      else
        llvm::outs() << constants::ToolName << ": '" << T
                     << "' is up to date.\n";
    }
    return 0;
  }

  JobScheduler::Options SchedOpts;
  SchedOpts.MaxJobs = Opts.Jobs;
  SchedOpts.DryRun = Opts.DryRun;
  SchedOpts.KeepGoing = Opts.KeepGoing;
  SchedOpts.Silent = Opts.Silent;

  JobScheduler Sched(SchedOpts);
  return Sched.execute(Graph, Env, Targets, &Rules);
}

} // namespace build
} // namespace neverc
