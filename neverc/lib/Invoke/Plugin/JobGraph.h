#ifndef NEVERC_INVOKE_PLUGIN_JOBGRAPH_H
#define NEVERC_INVOKE_PLUGIN_JOBGRAPH_H

#include "Plugin/ActionGraph.h"
#include "neverc/Invoke/Job.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace neverc::plugin {
class PluginSession;
}

namespace neverc::driver {

struct DriverJobFileRecord {
  std::string Path;
  NevercDriverType PublicType = NEVERC_DRIVER_TYPE_INVALID;
  types::ID InternalType = types::TY_INVALID;
};

struct DriverJobGraphNode {
  NevercJobID ID = 0;
  NevercJobKind Kind = 0;
  NevercActionNodeID SourceAction = 0;
  std::vector<NevercJobID> Dependencies;
  std::string Executable;
  std::vector<std::string> Arguments;
  std::vector<std::string> Environment;
  std::vector<DriverJobFileRecord> Inputs;
  std::vector<DriverJobFileRecord> Outputs;
  NevercResponseFileKind ResponseFileKind = NEVERC_RESPONSE_FILE_NONE;
  NevercResponseFileEncoding ResponseFileEncoding =
      NEVERC_RESPONSE_ENCODING_UTF8;
  bool InProcess = false;
  NevercLinkerFlavor LinkerFlavor = NEVERC_LINKER_FLAVOR_NONE;
  std::string PluginID;
  std::string CallbackID;
  NevercPluginJobCallbackFn Callback = nullptr;
  void *CallbackUserData = nullptr;
  Command *Original = nullptr;
  bool Modified = true;
};

struct DriverJobGraphData {
  std::vector<DriverJobGraphNode> Nodes;
};

class DriverJobGraphEdit;

class DriverJobGraphArtifact {
public:
  DriverJobGraphArtifact(
      DriverJobGraphData Graph,
      std::vector<DriverMaterializedAction> AllowedActions,
      std::vector<std::string> AllowedExternalInputs);
  DriverJobGraphArtifact(const DriverJobGraphArtifact &Other);

  llvm::Error verify() const;
  uint64_t jobCount() const;
  bool describeJob(uint64_t Index, NevercJob &OutJob) const;
  bool getDependency(NevercJobID Job, uint64_t Index,
                     NevercJobID &OutDependency) const;
  bool getArgument(NevercJobID Job, uint64_t Index,
                   NevercStringView &OutValue) const;
  bool getEnvironment(NevercJobID Job, uint64_t Index,
                      NevercStringView &OutValue) const;
  bool getInput(NevercJobID Job, uint64_t Index,
                NevercJobFile &OutFile) const;
  bool getOutput(NevercJobID Job, uint64_t Index,
                 NevercJobFile &OutFile) const;
  DriverJobGraphData snapshot() const;
  std::vector<DriverMaterializedAction> actionCatalog() const;

  llvm::Expected<std::unique_ptr<DriverJobGraphEdit>> beginMutation();

private:
  llvm::Error commitMutation(DriverJobGraphData Graph);
  void abortMutation();

  mutable std::mutex Mutex;
  DriverJobGraphData Graph;
  std::vector<DriverMaterializedAction> AllowedActions;
  std::vector<std::string> AllowedExternalInputs;
  bool MutationActive = false;

  friend class DriverJobGraphEdit;
};

class DriverJobGraphEdit {
public:
  static llvm::Expected<std::unique_ptr<DriverJobGraphEdit>>
  createBuilder(const DriverActionGraphArtifact &ActionGraph);
  ~DriverJobGraphEdit();

  DriverJobGraphEdit(const DriverJobGraphEdit &) = delete;
  DriverJobGraphEdit &operator=(const DriverJobGraphEdit &) = delete;

  llvm::Expected<NevercJobID> addJob(DriverJobGraphNode Node);
  llvm::Error removeJob(NevercJobID Job);
  llvm::Error moveJobBefore(NevercJobID Job, NevercJobID Before);
  llvm::Error replaceJob(NevercJobID Job, DriverJobGraphNode Node);
  llvm::Error setArgument(NevercJobID Job, uint64_t Index,
                          llvm::StringRef Value);
  llvm::Error setEnvironment(NevercJobID Job, uint64_t Index,
                             llvm::StringRef Value);
  llvm::Error setInput(NevercJobID Job, uint64_t Index,
                       DriverJobFileRecord File);
  llvm::Error setOutput(NevercJobID Job, uint64_t Index,
                        DriverJobFileRecord File);
  llvm::Error replaceDependencies(
      NevercJobID Job, llvm::ArrayRef<NevercJobID> Dependencies);

  bool isMutation() const { return Owner != nullptr; }
  llvm::Expected<std::unique_ptr<DriverJobGraphArtifact>> finishBuilder();
  llvm::Error commitMutation();
  void abort();

private:
  DriverJobGraphEdit(
      DriverJobGraphArtifact *Owner, DriverJobGraphData Graph,
      std::vector<DriverMaterializedAction> AllowedActions,
      std::vector<std::string> AllowedExternalInputs);

  DriverJobGraphNode *findJob(NevercJobID Job);

  DriverJobGraphArtifact *Owner = nullptr;
  DriverJobGraphData Graph;
  std::vector<DriverMaterializedAction> AllowedActions;
  std::vector<std::string> AllowedExternalInputs;
  NevercJobID NextJobID = 1;
  bool Finished = false;

  friend class DriverJobGraphArtifact;
};

struct DriverJobExecutionPlanNode {
  NevercJobID ID = 0;
  Command *Job = nullptr;
  std::vector<NevercJobID> Dependencies;
  DriverJobGraphNode Request;
};

class DriverJobExecutionPlan {
public:
  explicit DriverJobExecutionPlan(
      std::vector<DriverJobExecutionPlanNode> Nodes)
      : Nodes(std::move(Nodes)) {}

  llvm::ArrayRef<DriverJobExecutionPlanNode> nodes() const { return Nodes; }

private:
  std::vector<DriverJobExecutionPlanNode> Nodes;
};

struct DriverJobGraphArtifactTypes {
  std::shared_ptr<const plugin::PluginArtifactType> Graph;
};

NevercInterfaceID driverJobGraphArtifactID();
NevercInterfaceID driverBuildJobsPhaseID();

llvm::Expected<DriverJobGraphArtifactTypes>
registerDriverJobGraphArtifacts(plugin::PluginArtifactRegistry &Registry);

llvm::Expected<std::unique_ptr<DriverJobGraphArtifact>>
snapshotDriverJobGraph(Compilation &Compilation,
                       const DriverActionGraphArtifact &ActionGraph);

llvm::Expected<std::unique_ptr<DriverJobExecutionPlan>>
materializeDriverJobGraph(
    Compilation &Compilation, const DriverJobGraphArtifact &Graph,
    std::shared_ptr<plugin::PluginSession> Session);

} // namespace neverc::driver

#endif
