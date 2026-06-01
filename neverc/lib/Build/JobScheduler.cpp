#include "neverc/Build/JobScheduler.h"
#include "neverc/Build/Platform.h"

#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <future>

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

void JobScheduler::collectReadyJobs(DepGraph &Graph,
                                     const std::vector<std::string> &Targets,
                                     std::vector<Job> &Ready) {
  for (auto &[Name, N] : Graph.nodes()) {
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
      J.Target = Name;
      J.Node = &N;
      J.Recipes = N.Rule->Recipes;
      Ready.push_back(J);
    } else {
      N.Built = true;
    }
  }
}

int JobScheduler::runJob(Job &J, VariableEnv &Env) {
  // Set exported variables in the process environment so child commands see
  // them.
  for (auto &[Name, Var] : Env.vars()) {
    if (Var.Exported) {
#ifdef _WIN32
      _putenv_s(Name.c_str(), Var.Value.c_str());
#else
      setenv(Name.c_str(), Var.Value.c_str(), 1);
#endif
    }
  }

  for (auto &R : J.Recipes) {
    std::string Cmd = expandRecipeVars(R.Command, J.Target, *J.Node, Env);

    if (Cmd.empty())
      continue;

    bool Silent = R.Silent || Opts.Silent;
    bool IgnoreErr = R.IgnoreError;

    if (Opts.DryRun && !R.Force) {
      std::lock_guard<std::mutex> Lock(OutputMutex);
      llvm::outs() << Cmd << "\n";
      continue;
    }

    if (!Silent) {
      std::lock_guard<std::mutex> Lock(OutputMutex);
      llvm::outs() << Cmd << "\n";
    }

    int Rc = platform::shellExecuteNoCapture(Cmd, Opts.Shell, false);
    if (Rc != 0 && !IgnoreErr) {
      std::lock_guard<std::mutex> Lock(OutputMutex);
      llvm::errs() << "neverc make: *** [" << J.Target
                   << "] Error " << Rc << "\n";
      return Rc;
    }
  }
  return 0;
}

int JobScheduler::execute(DepGraph &Graph, VariableEnv &Env,
                           const std::vector<std::string> &Targets) {
  bool AnyFailed = false;

  while (true) {
    std::vector<Job> Ready;
    collectReadyJobs(Graph, Targets, Ready);

    if (Ready.empty())
      break;

    if (Opts.MaxJobs <= 1) {
      for (auto &J : Ready) {
        int Rc = runJob(J, Env);
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
                                     [this, &Ready, I, &Env]() {
                                       VariableEnv LocalEnv(Env);
                                       return runJob(Ready[I], LocalEnv);
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
        int Rc = runJob(Ready[I], Env);
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
      llvm::errs() << "neverc make: Nothing to be done for '" << Target
                   << "'.\n";
    }
  }

  return AnyFailed ? 2 : 0;
}

} // namespace build
} // namespace neverc
