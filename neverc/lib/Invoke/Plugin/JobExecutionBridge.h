#ifndef NEVERC_INVOKE_PLUGIN_JOBEXECUTIONBRIDGE_H
#define NEVERC_INVOKE_PLUGIN_JOBEXECUTIONBRIDGE_H

#include "Plugin/JobGraph.h"
#include "neverc/Plugin/Host/PluginArtifactRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace neverc::plugin {
class PluginSession;
class PluginTaskContext;
}

namespace neverc::driver {

class Command;
class DriverAPIBridge;

struct DriverJobExecutionOutcome {
  int32_t ExitCode = 0;
  bool ExecutionFailed = false;
  std::string ErrorMessage;
  bool HasProcessStatistics = false;
  uint64_t TotalTimeMicroseconds = 0;
  uint64_t UserTimeMicroseconds = 0;
  uint64_t PeakMemoryKiB = 0;
};

class DriverJobRequestArtifact {
public:
  explicit DriverJobRequestArtifact(DriverJobGraphNode Job);

  llvm::Error verify() const;
  void describe(NevercJobExecutionRequest &OutRequest) const;
  const DriverJobGraphNode &job() const { return Job; }

private:
  void rebuildViews();

  DriverJobGraphNode Job;
  std::vector<NevercStringView> ArgumentViews;
  std::vector<NevercStringView> EnvironmentViews;
  std::vector<NevercJobFile> InputViews;
  std::vector<NevercJobFile> OutputViews;
};

class DriverJobResultArtifact {
public:
  DriverJobResultArtifact(const DriverJobRequestArtifact &Request,
                          DriverJobExecutionOutcome Outcome,
                          bool BuiltinProviderUsed,
                          std::vector<NevercOutputSealHandle> OutputSeals = {});

  llvm::Error verify() const;
  llvm::Error
  commitReplacementOutputs(plugin::PluginTaskContext &Task) const;
  void describe(NevercJobResult &OutResult) const;
  const DriverJobExecutionOutcome &outcome() const { return Outcome; }

private:
  NevercJobID Job = 0;
  std::vector<DriverJobFileRecord> DeclaredOutputs;
  std::vector<NevercOutputSealHandle> OutputSeals;
  DriverJobExecutionOutcome Outcome;
  bool BuiltinProviderUsed = false;
};

struct DriverJobExecutionArtifactTypes {
  std::shared_ptr<const plugin::PluginArtifactType> Request;
  std::shared_ptr<const plugin::PluginArtifactType> Result;
};

NevercInterfaceID driverJobRequestArtifactID();
NevercInterfaceID driverJobResultArtifactID();
NevercInterfaceID driverExecuteJobPhaseID();

llvm::Expected<DriverJobExecutionArtifactTypes>
registerDriverJobExecutionArtifacts(
    plugin::PluginArtifactRegistry &Registry);

class DriverJobExecutionRuntime {
public:
  DriverJobExecutionRuntime(
      DriverAPIBridge &Bridge,
      std::shared_ptr<plugin::PluginSession> Session,
      plugin::PluginTaskContext &Task, std::string TargetTriple,
      std::string ObjectFormat, NevercExecutionLevel ExecutionLevel);

  llvm::Expected<DriverJobExecutionOutcome>
  execute(const DriverJobGraphNode &Request, const Command &Job,
          llvm::ArrayRef<llvm::StringRef> Redirects,
          bool &OutBuiltinProviderInvoked);

private:
  DriverAPIBridge &Bridge;
  std::shared_ptr<plugin::PluginSession> Session;
  plugin::PluginTaskContext &Task;
  std::string TargetTriple;
  std::string ObjectFormat;
  NevercExecutionLevel ExecutionLevel;
};

} // namespace neverc::driver

#endif
