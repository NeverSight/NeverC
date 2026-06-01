#include "neverc/Build/BuildDriver.h"
#include "neverc/Build/AST.h"
#include "neverc/Build/DepGraph.h"
#include "neverc/Build/Function.h"
#include "neverc/Build/JobScheduler.h"
#include "neverc/Build/Lexer.h"
#include "neverc/Build/Parser.h"
#include "neverc/Build/Platform.h"
#include "neverc/Build/RuleDB.h"
#include "neverc/Build/VariableEnv.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_set>
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

bool parseOptions(int Argc, const char **Argv, BuildOptions &Opts) {
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
      return false;
    } else if (Arg.find('=') != std::string::npos) {
      size_t Eq = Arg.find('=');
      Opts.CmdVars.emplace_back(Arg.substr(0, Eq), Arg.substr(Eq + 1));
    } else if (!Arg.empty() && Arg[0] != '-') {
      Opts.Targets.push_back(Arg);
    } else {
      llvm::errs() << "neverc make: Unknown option: " << Arg << "\n";
    }
    ++I;
  }
  if (Opts.Jobs == 0)
    Opts.Jobs = 1;
  return true;
}

std::string findMakefile(const std::string &Specified) {
  if (!Specified.empty()) {
    if (platform::fileExists(Specified))
      return Specified;
    llvm::errs() << "neverc make: " << Specified
                 << ": No such file or directory\n";
    return "";
  }

  if (platform::fileExists("GNUmakefile"))
    return "GNUmakefile";
  if (platform::fileExists("makefile"))
    return "makefile";
  if (platform::fileExists("Makefile"))
    return "Makefile";

  llvm::errs() << "neverc make: *** No targets specified and no makefile "
                  "found. Stop.\n";
  return "";
}

void processAST(MakefileAST &AST, VariableEnv &Env, RuleDB &Rules,
                FunctionRegistry &FuncReg);

void processStatements(const std::vector<std::unique_ptr<Statement>> &Stmts,
                       VariableEnv &Env, RuleDB &Rules,
                       FunctionRegistry &FuncReg) {
  for (auto &S : Stmts) {
    switch (S->Kind) {
    case StmtKind::VarAssign: {
      auto *VA = static_cast<VarAssign *>(S.get());
      std::string Name = Env.expand(VA->Name);
      std::string Value = VA->RawValue;
      VariableEnv::Origin Orig = VA->Override
                                     ? VariableEnv::Origin::Override
                                     : VariableEnv::Origin::File;
      switch (VA->Mode) {
      case AssignMode::Recursive:
        Env.set(Name, Value, AssignMode::Recursive, Orig);
        break;
      case AssignMode::Simple:
        Env.set(Name, Env.expand(Value), AssignMode::Simple, Orig);
        break;
      case AssignMode::Conditional:
        Env.conditionalSet(Name, Value);
        break;
      case AssignMode::Append:
        Env.append(Name, Value);
        break;
      case AssignMode::Shell: {
        auto R = platform::shellExecute(Value);
        std::string Out = R.Output;
        std::replace(Out.begin(), Out.end(), '\n', ' ');
        Env.set(Name, Out, AssignMode::Simple, Orig);
        break;
      }
      }
      if (VA->Export)
        Env.setExport(Name);
      break;
    }
    case StmtKind::Rule: {
      auto *R = static_cast<Rule *>(S.get());
      Rules.addRule(*R, Env);
      break;
    }
    case StmtKind::Conditional: {
      auto *C = static_cast<Conditional *>(S.get());
      bool Result = false;
      switch (C->CondKind) {
      case Conditional::IfEq:
        Result = Env.expand(C->Arg1) == Env.expand(C->Arg2);
        break;
      case Conditional::IfNeq:
        Result = Env.expand(C->Arg1) != Env.expand(C->Arg2);
        break;
      case Conditional::IfDef: {
        std::string Name = Env.expand(C->Arg1);
        Result = Env.isDefined(Name) && !Env.rawValue(Name).empty();
        break;
      }
      case Conditional::IfNDef: {
        std::string Name = Env.expand(C->Arg1);
        Result = !Env.isDefined(Name) || Env.rawValue(Name).empty();
        break;
      }
      }
      if (Result)
        processStatements(C->ThenBranch, Env, Rules, FuncReg);
      else
        processStatements(C->ElseBranch, Env, Rules, FuncReg);
      break;
    }
    case StmtKind::Include: {
      auto *Inc = static_cast<Include *>(S.get());
      for (auto &F : Inc->Files) {
        std::string Expanded = Env.expand(F);

        std::vector<std::string> Paths;
        if (Expanded.find('*') != std::string::npos ||
            Expanded.find('?') != std::string::npos) {
          Paths = platform::globFiles(Expanded);
        } else {
          Paths.push_back(Expanded);
        }

        for (auto &Path : Paths) {
          auto Buf = llvm::MemoryBuffer::getFile(Path);
          if (!Buf) {
            if (!Inc->Optional)
              llvm::errs() << "neverc make: " << Path
                           << ": No such file or directory\n";
            continue;
          }
          std::string Content = (*Buf)->getBuffer().str();
          Lexer L(Path, Content);
          auto Lines = L.lex();
          Parser P(Path, std::move(Lines));
          auto SubAST = P.parse();
          if (SubAST)
            processAST(*SubAST, Env, Rules, FuncReg);
        }
      }
      break;
    }
    case StmtKind::DefineBlock: {
      auto *D = static_cast<DefineBlock *>(S.get());
      VariableEnv::Origin Orig =
          D->Override ? VariableEnv::Origin::Override
                      : VariableEnv::Origin::File;
      switch (D->Mode) {
      case AssignMode::Simple:
        Env.set(D->Name, Env.expand(D->Body), AssignMode::Simple, Orig);
        break;
      case AssignMode::Append:
        Env.append(D->Name, D->Body);
        break;
      case AssignMode::Conditional:
        Env.conditionalSet(D->Name, D->Body);
        break;
      default:
        Env.set(D->Name, D->Body, AssignMode::Recursive, Orig);
        break;
      }
      break;
    }
    case StmtKind::ExportDirective: {
      auto *E = static_cast<ExportDirective *>(S.get());
      if (E->IsUnexport) {
        for (auto &Name : E->Names)
          Env.setExport(Env.expand(Name), false);
      } else if (E->ExportAll) {
        for (auto &[Name, Var] : Env.vars())
          Env.setExport(Name);
      } else {
        for (auto &Name : E->Names)
          Env.setExport(Env.expand(Name));
      }
      break;
    }
    case StmtKind::Expression: {
      auto *E = static_cast<Expression *>(S.get());
      Env.expand(E->Text);
      break;
    }
    }
  }
}

void processAST(MakefileAST &AST, VariableEnv &Env, RuleDB &Rules,
                FunctionRegistry &FuncReg) {
  processStatements(AST.Stmts, Env, Rules, FuncReg);
}

void printDatabase(const RuleDB &Rules, const VariableEnv &Env) {
  llvm::outs() << "# Variables\n";
  for (auto &[Name, Var] : Env.vars())
    llvm::outs() << Name << " = " << Var.Value << "\n";
  llvm::outs() << "\n# Rules\n";
  for (auto &[Target, RuleList] : Rules.rules()) {
    for (auto &R : RuleList) {
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

int runBuild(int Argc, const char **Argv, const char *Argv0) {
  BuildOptions Opts;
  if (!parseOptions(Argc, Argv, Opts))
    return 0;

  for (auto &Dir : Opts.ChangeDirs) {
    if (!platform::changeCwd(Dir)) {
      llvm::errs() << "neverc make: *** No such directory: "
                   << Dir << "\n";
      return 2;
    }
  }

  std::string MakefilePath = findMakefile(Opts.MakefilePath);
  if (MakefilePath.empty())
    return 2;

  auto Buf = llvm::MemoryBuffer::getFile(MakefilePath);
  if (!Buf) {
    llvm::errs() << "neverc make: *** Cannot read " << MakefilePath << "\n";
    return 2;
  }

  std::string Content = (*Buf)->getBuffer().str();

  VariableEnv Env;
  FunctionRegistry FuncReg;
  RuleDB Rules;
  Env.setFunctionRegistry(&FuncReg);
  Env.importEnvironment();

  Env.setEvalCallback([&Env, &Rules, &FuncReg](const std::string &Text) {
    Lexer EvalL("<eval>", Text);
    auto EvalLines = EvalL.lex();
    Parser EvalP("<eval>", std::move(EvalLines));
    auto EvalAST = EvalP.parse();
    if (EvalAST)
      processAST(*EvalAST, Env, Rules, FuncReg);
  });

  for (auto &[Name, Value] : Opts.CmdVars)
    Env.setCommandLineVar(Name, Value);

  Env.set("MAKEFILE_LIST", MakefilePath, AssignMode::Simple,
           VariableEnv::Origin::Default);
  Env.set("CURDIR", platform::getCwd(), AssignMode::Simple,
           VariableEnv::Origin::Default);
  Env.set("MAKE", std::string(Argv0) + " make", AssignMode::Simple,
           VariableEnv::Origin::Default);

  {
    std::string Flags;
    if (Opts.DryRun) Flags += 'n';
    if (Opts.KeepGoing) Flags += 'k';
    if (Opts.Silent) Flags += 's';
    if (Opts.AlwaysMake) Flags += 'B';
    if (Opts.Jobs > 1)
      Flags += " -j" + std::to_string(Opts.Jobs);
    for (auto &[Name, Value] : Opts.CmdVars)
      Flags += " " + Name + "=" + Value;
    Env.set("MAKEFLAGS", Flags, AssignMode::Simple,
             VariableEnv::Origin::Default);
  }

  if (!Opts.Targets.empty()) {
    std::string Goals;
    for (size_t I = 0; I < Opts.Targets.size(); ++I) {
      if (I > 0)
        Goals += ' ';
      Goals += Opts.Targets[I];
    }
    Env.set("MAKECMDGOALS", Goals, AssignMode::Simple,
             VariableEnv::Origin::Default);
  }

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

  processAST(*AST, Env, Rules, FuncReg);

  if (Opts.PrintDataBase) {
    printDatabase(Rules, Env);
    return 0;
  }

  std::vector<std::string> Targets = Opts.Targets;
  if (Targets.empty()) {
    std::string Default;
    std::string DGVar = Env.get(".DEFAULT_GOAL");
    if (!DGVar.empty())
      Default = Env.expand(DGVar);
    if (Default.empty())
      Default = Rules.defaultTarget();
    if (Default.empty()) {
      llvm::errs() << "neverc make: *** No targets. Stop.\n";
      return 2;
    }
    Targets.push_back(Default);
  }

  DepGraph Graph;
  for (auto &T : Targets) {
    if (!Graph.build(T, Rules, Opts.AlwaysMake)) {
      if (Graph.hasCycle())
        llvm::errs() << "neverc make: " << Graph.cycleMessage() << "\n";
      return 2;
    }
  }

  for (auto &T : Targets) {
    auto *N = Graph.getNode(T);
    if (N && !N->Rule && !N->IsPhony && !platform::fileExists(T)) {
      llvm::errs() << "neverc make: *** No rule to make target '"
                   << T << "'.  Stop.\n";
      return 2;
    }
  }

  {
    std::unordered_set<std::string> AllPrereqs;
    for (auto &[N, Node] : Graph.nodes())
      for (auto &Dep : Node.Dependencies)
        AllPrereqs.insert(Dep);

    for (auto &[Name, Node] : Graph.nodes()) {
      if (Node.Rule || Node.IsPhony)
        continue;
      if (platform::fileExists(Name))
        continue;
      if (AllPrereqs.count(Name)) {
        llvm::errs() << "neverc make: *** No rule to make target '"
                     << Name << "'.  Stop.\n";
        return 2;
      }
    }
  }

  bool NothingToDo = true;
  for (auto &T : Targets) {
    auto *N = Graph.getNode(T);
    if (!N)
      continue;
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
  if (NothingToDo && !Opts.AlwaysMake) {
    for (auto &T : Targets) {
      auto *N = Graph.getNode(T);
      if (N && N->IsPhony)
        llvm::outs() << "neverc make: Nothing to be done for '" << T
                     << "'.\n";
      else
        llvm::outs() << "neverc make: '" << T << "' is up to date.\n";
    }
    return 0;
  }

  JobScheduler::Options SchedOpts;
  SchedOpts.MaxJobs = Opts.Jobs;
  SchedOpts.DryRun = Opts.DryRun;
  SchedOpts.KeepGoing = Opts.KeepGoing;
  SchedOpts.Silent = Opts.Silent;

  JobScheduler Sched(SchedOpts);
  return Sched.execute(Graph, Env, Targets);
}

} // namespace build
} // namespace neverc
