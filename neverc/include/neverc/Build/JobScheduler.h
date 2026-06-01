#ifndef NEVERC_BUILD_JOBSCHEDULER_H
#define NEVERC_BUILD_JOBSCHEDULER_H

#include "neverc/Build/DepGraph.h"
#include "neverc/Build/VariableEnv.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace neverc {
namespace build {

class JobScheduler {
public:
  struct Options {
    unsigned MaxJobs = 1;
    bool DryRun = false;
    bool KeepGoing = false;
    bool Silent = false;
    std::string Shell;
  };

  explicit JobScheduler(const Options &Opts);

  int execute(DepGraph &Graph, VariableEnv &Env,
              const std::vector<std::string> &Targets);

private:
  struct Job {
    std::string Target;
    DepGraph::Node *Node;
    std::vector<Recipe> Recipes;
  };

  int runJob(Job &J, VariableEnv &Env);
  std::string expandRecipeVars(const std::string &Cmd,
                                const std::string &Target,
                                const DepGraph::Node &N,
                                VariableEnv &Env);
  void collectReadyJobs(DepGraph &Graph,
                        const std::vector<std::string> &Targets,
                        std::vector<Job> &Ready);
  bool allDepsBuilt(const DepGraph::Node &N, const DepGraph &Graph) const;

  Options Opts;
  std::mutex OutputMutex;
  std::atomic<int> FailCount{0};
};

} // namespace build
} // namespace neverc

#endif // NEVERC_BUILD_JOBSCHEDULER_H
