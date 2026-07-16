#ifndef NEVERC_INVOKE_PLUGIN_ACTIONGRAPH_H
#define NEVERC_INVOKE_PLUGIN_ACTIONGRAPH_H

#include "neverc/Invoke/Types.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace llvm::opt {
class Arg;
}

namespace neverc::driver {

class Action;
class Compilation;

struct DriverActionInputRecord {
  NevercDriverInputID ID = 0;
  NevercDriverType PublicType = NEVERC_DRIVER_TYPE_INVALID;
  types::ID InternalType = types::TY_INVALID;
  const llvm::opt::Arg *Argument = nullptr;
  std::string Value;
};

class DriverActionGraphRequestArtifact {
public:
  llvm::Expected<NevercDriverInputID>
  addInput(types::ID Type, const llvm::opt::Arg &Argument);
  llvm::Expected<NevercDriverInputID>
  ensureInput(types::ID Type, const llvm::opt::Arg &Argument);

  llvm::Error verify() const;
  uint64_t inputCount() const;
  bool describeInput(uint64_t Index, NevercDriverInput &OutInput) const;
  std::vector<DriverActionInputRecord> snapshotInputs() const;
  bool findInput(NevercDriverInputID ID,
                 DriverActionInputRecord &OutInput) const;

private:
  mutable std::mutex Mutex;
  std::vector<DriverActionInputRecord> Inputs;
  NevercDriverInputID NextInputID = 1;
};

struct DriverActionGraphNode {
  NevercActionNodeID ID = 0;
  NevercActionKind Kind = NEVERC_ACTION_INVALID;
  NevercDriverType OutputType = NEVERC_DRIVER_TYPE_INVALID;
  std::vector<NevercActionNodeID> Inputs;
  NevercDriverInputID DriverInput = 0;
  std::string BindArch;
};

struct DriverActionGraphData {
  std::vector<DriverActionGraphNode> Nodes;
  std::vector<NevercActionNodeID> Roots;
};

struct DriverMaterializedAction {
  NevercActionNodeID ID = 0;
  Action *Value = nullptr;
};

class DriverActionGraphEdit;

class DriverActionGraphArtifact {
public:
  DriverActionGraphArtifact(
      DriverActionGraphData Graph,
      std::vector<DriverActionInputRecord> AllowedInputs);
  DriverActionGraphArtifact(const DriverActionGraphArtifact &Other);

  llvm::Error verify() const;
  uint64_t nodeCount() const;
  uint64_t rootCount() const;
  bool describeNode(uint64_t Index, NevercActionNode &OutNode) const;
  bool getNodeInput(NevercActionNodeID Node, uint64_t Index,
                    NevercActionNodeID &OutInput) const;
  bool getRoot(uint64_t Index, NevercActionNodeID &OutRoot) const;
  DriverActionGraphData snapshot() const;
  std::vector<DriverActionInputRecord> inputCatalog() const;
  void setMaterializedActions(
      std::vector<DriverMaterializedAction> Actions);
  std::vector<DriverMaterializedAction> materializedActions() const;
  Action *findMaterializedAction(NevercActionNodeID ID) const;

  llvm::Expected<std::unique_ptr<DriverActionGraphEdit>> beginMutation();

private:
  llvm::Error commitMutation(DriverActionGraphData Graph);
  void abortMutation();

  mutable std::mutex Mutex;
  DriverActionGraphData Graph;
  std::vector<DriverActionInputRecord> AllowedInputs;
  std::vector<DriverMaterializedAction> MaterializedActions;
  bool MutationActive = false;

  friend class DriverActionGraphEdit;
};

class DriverActionGraphEdit {
public:
  static llvm::Expected<std::unique_ptr<DriverActionGraphEdit>>
  createBuilder(const DriverActionGraphRequestArtifact &Request);
  ~DriverActionGraphEdit();

  DriverActionGraphEdit(const DriverActionGraphEdit &) = delete;
  DriverActionGraphEdit &operator=(const DriverActionGraphEdit &) = delete;

  llvm::Expected<NevercActionNodeID>
  addNode(NevercActionKind Kind, NevercDriverType OutputType,
          NevercDriverInputID DriverInput, llvm::StringRef BindArch,
          llvm::ArrayRef<NevercActionNodeID> Inputs);
  llvm::Error removeNode(NevercActionNodeID Node);
  llvm::Error replaceInputs(NevercActionNodeID Node,
                            llvm::ArrayRef<NevercActionNodeID> Inputs);
  llvm::Error setOutputType(NevercActionNodeID Node,
                            NevercDriverType OutputType);
  llvm::Error setBindArch(NevercActionNodeID Node, llvm::StringRef BindArch);
  llvm::Error setRoots(llvm::ArrayRef<NevercActionNodeID> Roots);

  bool isMutation() const { return Owner != nullptr; }
  llvm::Expected<std::unique_ptr<DriverActionGraphArtifact>> finishBuilder();
  llvm::Error commitMutation();
  void abort();

private:
  DriverActionGraphEdit(
      DriverActionGraphArtifact *Owner, DriverActionGraphData Graph,
      std::vector<DriverActionInputRecord> AllowedInputs);

  DriverActionGraphNode *findNode(NevercActionNodeID Node);

  DriverActionGraphArtifact *Owner = nullptr;
  DriverActionGraphData Graph;
  std::vector<DriverActionInputRecord> AllowedInputs;
  NevercActionNodeID NextNodeID = 1;
  bool Finished = false;

  friend class DriverActionGraphArtifact;
};

struct DriverActionGraphArtifactTypes {
  std::shared_ptr<const plugin::PluginArtifactType> Request;
  std::shared_ptr<const plugin::PluginArtifactType> Graph;
};

NevercInterfaceID driverActionGraphRequestArtifactID();
NevercInterfaceID driverActionGraphArtifactID();
NevercInterfaceID driverBuildActionsPhaseID();

llvm::Expected<NevercDriverType> toPublicDriverType(types::ID Type);
llvm::Expected<types::ID> toInternalDriverType(NevercDriverType Type);

llvm::Expected<DriverActionGraphArtifactTypes>
registerDriverActionGraphArtifacts(plugin::PluginArtifactRegistry &Registry);

llvm::Expected<std::unique_ptr<DriverActionGraphArtifact>>
snapshotDriverActionGraph(DriverActionGraphRequestArtifact &Request,
                          llvm::ArrayRef<Action *> Roots);

llvm::Error
materializeDriverActionGraph(Compilation &Compilation,
                             const DriverActionGraphRequestArtifact &Request,
                             DriverActionGraphArtifact &Graph);

} // namespace neverc::driver

#endif
