#include "neverc/Build/JobScheduler.h"
#include "neverc/Build/BuildConstants.h"
#include "neverc/Build/BuiltinCommands.h"
#include "neverc/Build/Platform.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <future>
#include <sstream>

namespace neverc {
namespace build {

JobScheduler::JobScheduler(const Options &Opts) : Opts(Opts) {}

std::string JobScheduler::expandRecipeVars(const std::string &Cmd,
                                            const std::string &Target,
                                            const DepGraph::Node &N,
                                            VariableEnv &Env) {
  Env.setAutoVar("@", Target);

  if (!N.Dependencies.empty())
    Env.setAutoVar("<", N.Dependencies[0]);

  {
    std::string AllPrereqs;
    for (size_t I = 0; I < N.Dependencies.size(); ++I) {
      if (I > 0)
        AllPrereqs += ' ';
      AllPrereqs += N.Dependencies[I];
    }
    Env.setAutoVar("^", AllPrereqs);
  }

  {
    std::string NewerPrereqs;
    int64_t TargetTime = platform::getFileTimestamp(Target);
    for (auto &Dep : N.Dependencies) {
      int64_t DepTime = platform::getFileTimestamp(Dep);
      if (DepTime < 0)
        continue;
      if (TargetTime < 0 || DepTime > TargetTime) {
        if (!NewerPrereqs.empty())
          NewerPrereqs += ' ';
        NewerPrereqs += Dep;
      }
    }
    if (N.IsPhony) {
      NewerPrereqs.clear();
      for (size_t I = 0; I < N.Dependencies.size(); ++I) {
        if (I > 0)
          NewerPrereqs += ' ';
        NewerPrereqs += N.Dependencies[I];
      }
    }
    Env.setAutoVar("?", NewerPrereqs);
  }

  if (N.Rule && !N.Rule->Stem.empty())
    Env.setAutoVar("*", N.Rule->Stem);

  // $(@D), $(@F)
  {
    size_t Slash = Target.find_last_of("/\\");
    if (Slash != std::string::npos) {
      Env.setAutoVar("@D", Target.substr(0, Slash));
      Env.setAutoVar("@F", Target.substr(Slash + 1));
    } else {
      Env.setAutoVar("@D", ".");
      Env.setAutoVar("@F", Target);
    }
  }

  std::string Expanded = Env.expand(Cmd);
  Env.clearAutoVars();
  return Expanded;
}

bool JobScheduler::allDepsBuilt(const DepGraph::Node &N,
                                 const DepGraph &Graph) const {
  for (auto &Dep : N.Dependencies) {
    auto *DepNode = Graph.getNode(Dep);
    if (DepNode && DepNode->NeedsBuild && !DepNode->Built)
      return false;
    if (DepNode && DepNode->Failed)
      return false;
  }
  for (auto &Dep : N.OrderOnlyDeps) {
    auto *DepNode = Graph.getNode(Dep);
    if (DepNode && DepNode->NeedsBuild && !DepNode->Built)
      return false;
    if (DepNode && DepNode->Failed)
      return false;
  }
  return true;
}

bool JobScheduler::collectReadyJobs(DepGraph &Graph,
                                     const std::vector<std::string> &Targets,
                                     std::vector<Job> &Ready) {
  bool MadeProgress = false;
  for (auto &Entry : Graph.nodes()) {
    auto &N = Entry.second;
    if (!N.NeedsBuild || N.Built || N.Failed)
      continue;
    if (!allDepsBuilt(N, Graph))
      continue;

    bool HasFailedDep = false;
    for (auto &Dep : N.Dependencies) {
      auto *DepNode = Graph.getNode(Dep);
      if (DepNode && DepNode->Failed) {
        HasFailedDep = true;
        break;
      }
    }
    if (HasFailedDep) {
      N.Failed = true;
      continue;
    }

    if (N.Rule && !N.Rule->Recipes.empty()) {
      Job J;
      J.Target = Entry.first().str();
      J.Node = &N;
      J.Recipes = N.Rule->Recipes;
      Ready.push_back(J);
    } else {
      N.Built = true;
      MadeProgress = true;
    }
  }
  return MadeProgress;
}

int JobScheduler::runJob(Job &J, VariableEnv &Env, const RuleDB *Rules) {
  // Apply target-specific variable overrides.
  struct SavedVar {
    std::string Name;
    std::string Value;
    bool WasDefined;
  };
  std::vector<SavedVar> Saved;
  if (Rules) {
    if (auto *TVars = Rules->getTargetVars(J.Target)) {
      for (auto &TV : *TVars) {
        std::string Name = Env.expand(TV.VarName);
        bool Defined = Env.isDefined(Name);
        Saved.push_back({Name, Defined ? Env.rawValue(Name) : "", Defined});
        if (TV.Mode == AssignMode::Conditional) {
          if (!Defined)
            Env.setForced(Name, TV.RawValue, AssignMode::Recursive);
        } else if (TV.Mode == AssignMode::Append) {
          Env.append(Name, Env.expand(TV.RawValue));
        } else {
          std::string Val = TV.RawValue;
          if (TV.Mode == AssignMode::Simple)
            Val = Env.expand(Val);
          Env.setForced(Name, Val, TV.Mode);
        }
      }
    }
  }

  // Set exported variables in the process environment so child commands see
  // them.
  for (auto &Entry : Env.vars()) {
    if (Entry.second.Exported) {
      std::string Key = Entry.first().str();
#ifdef _WIN32
      _putenv_s(Key.c_str(), Entry.second.Value.c_str());
#else
      setenv(Key.c_str(), Entry.second.Value.c_str(), 1);
#endif
    }
  }

  for (auto &R : J.Recipes) {
    std::string FullCmd = expandRecipeVars(R.Command, J.Target, *J.Node, Env);

    if (FullCmd.empty())
      continue;

    // $(call) / $(eval) can produce multiline commands from define
    // blocks. Split on newlines and execute each line independently.
    std::vector<std::string> Lines;
    {
      std::istringstream SS(FullCmd);
      std::string Line;
      while (std::getline(SS, Line))
        Lines.push_back(Line);
    }

    for (auto &Cmd : Lines) {
      if (Cmd.empty())
        continue;

      bool Silent = R.Silent || Opts.Silent;
      bool IgnoreErr = R.IgnoreError;
      bool Force = R.Force;

      while (!Cmd.empty() &&
             (Cmd[0] == '@' || Cmd[0] == '-' || Cmd[0] == '+')) {
        if (Cmd[0] == '@')
          Silent = true;
        else if (Cmd[0] == '-')
          IgnoreErr = true;
        else if (Cmd[0] == '+')
          Force = true;
        Cmd = Cmd.substr(1);
      }

      if (Cmd.empty())
        continue;

      if (Opts.DryRun && !Force) {
        std::lock_guard<std::mutex> Lock(OutputMutex);
        llvm::outs() << Cmd << "\n";
        continue;
      }

      int Rc = 0;
      // Echo under the builtin I/O lock when the command name is a known
      // builtin so `-j` recipe lines cannot interleave with builtin I/O.
      bool Echoed = false;
      if (!builtins::tryExecute(Cmd, Rc, /*EchoCommand=*/!Silent, &Echoed)) {
        if (!Silent && !Echoed) {
          std::lock_guard<std::mutex> Lock(OutputMutex);
          llvm::outs() << Cmd << "\n";
        }
        Rc = platform::shellExecuteNoCapture(Cmd, Opts.Shell, false);
      }
      if (Rc != 0 && !IgnoreErr) {
        std::lock_guard<std::mutex> Lock(OutputMutex);
        llvm::errs() << constants::ErrorPrefix << "[" << J.Target
                     << "] Error " << Rc << "\n";
        for (auto &S : Saved) {
          if (S.WasDefined)
            Env.setForced(S.Name, S.Value, AssignMode::Recursive);
          else
            Env.undefine(S.Name);
        }
        return Rc;
      }
    }
  }

  for (auto &S : Saved) {
    if (S.WasDefined)
      Env.setForced(S.Name, S.Value, AssignMode::Recursive);
    else
      Env.undefine(S.Name);
  }
  return 0;
}

int JobScheduler::execute(DepGraph &Graph, VariableEnv &Env,
                           const std::vector<std::string> &Targets,
                           const RuleDB *Rules) {
  bool AnyFailed = false;

  while (true) {
    std::vector<Job> Ready;
    bool MadeProgress = collectReadyJobs(Graph, Targets, Ready);

    if (Ready.empty()) {
      if (MadeProgress)
        continue;
      break;
    }

    if (Opts.MaxJobs <= 1) {
      for (auto &J : Ready) {
        int Rc = runJob(J, Env, Rules);
        if (Rc != 0) {
          J.Node->Failed = true;
          AnyFailed = true;
          ++FailCount;
          if (!Opts.KeepGoing)
            return 2;
        } else {
          J.Node->Built = true;
        }
      }
    } else {
      std::vector<std::future<int>> Futures;
      std::vector<Job *> JobPtrs;

      size_t BatchSize = std::min((size_t)Opts.MaxJobs, Ready.size());
      for (size_t I = 0; I < BatchSize; ++I) {
        JobPtrs.push_back(&Ready[I]);
        // Each async job gets its own VariableEnv copy to avoid
        // auto-variable race conditions.
        Futures.push_back(std::async(std::launch::async,
                                     [this, &Ready, I, &Env, Rules]() {
                                       VariableEnv LocalEnv(Env);
                                       return runJob(Ready[I], LocalEnv, Rules);
                                     }));
      }

      for (size_t I = 0; I < Futures.size(); ++I) {
        int Rc = Futures[I].get();
        if (Rc != 0) {
          JobPtrs[I]->Node->Failed = true;
          AnyFailed = true;
          ++FailCount;
          if (!Opts.KeepGoing)
            return 2;
        } else {
          JobPtrs[I]->Node->Built = true;
        }
      }

      for (size_t I = BatchSize; I < Ready.size(); ++I) {
        int Rc = runJob(Ready[I], Env, Rules);
        if (Rc != 0) {
          Ready[I].Node->Failed = true;
          AnyFailed = true;
          ++FailCount;
          if (!Opts.KeepGoing)
            return 2;
        } else {
          Ready[I].Node->Built = true;
        }
      }
    }
  }

  for (auto &Target : Targets) {
    auto *N = Graph.getNode(Target);
    if (N && N->NeedsBuild && !N->Built) {
      if (N->Failed)
        continue;
      llvm::errs() << constants::ToolName
                   << ": Nothing to be done for '" << Target << "'.\n";
    }
  }

  return AnyFailed ? 2 : 0;
}

} // namespace build
} // namespace neverc
