#ifndef NEVERC_PLUGIN_HOST_CALLINGCONVENTIONPLAN_H
#define NEVERC_PLUGIN_HOST_CALLINGCONVENTIONPLAN_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct CallingConventionPlanLocation {
  NevercCallingConventionLocationKind Kind = 0;
  uint32_t ValueIndex = 0;
  uint32_t PieceOffset = 0;
  uint32_t Size = 0;
  uint32_t Alignment = 0;
  uint32_t RegisterNumber = 0;
  uint32_t StackOffset = 0;
  NevercCallingConventionLocationFlags Flags = 0;
};

struct MaterializedCallingConventionPlan {
  NevercTargetID TargetID{};
  NevercCallingConventionID CallingConventionID{};
  std::string SchemaDigest;
  std::vector<CallingConventionPlanLocation> ReturnLocations;
  std::vector<CallingConventionPlanLocation> ArgumentLocations;
  std::vector<uint32_t> CalleeSavedRegisters;
  uint32_t StackAlignment = 0;

  std::string serialize() const;
};

class CallingConventionPlanner {
public:
  CallingConventionPlanner(
      const PluginTargetSnapshot::NamedRecord &Convention,
      const PluginTargetSnapshot::TargetRecord &Target,
      PluginTaskContext *Task = nullptr)
      : Convention(Convention), Target(Target), Task(Task) {}

  llvm::Expected<MaterializedCallingConventionPlan> materialize(
      const NevercABITypeDescriptor &ReturnType,
      llvm::ArrayRef<NevercABITypeDescriptor> Parameters,
      bool Variadic, uint32_t RequiredArguments) const;

private:
  const PluginTargetSnapshot::NamedRecord &Convention;
  const PluginTargetSnapshot::TargetRecord &Target;
  PluginTaskContext *Task;
};

} // namespace neverc::plugin

#endif
