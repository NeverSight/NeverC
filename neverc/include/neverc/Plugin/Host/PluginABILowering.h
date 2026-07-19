#ifndef NEVERC_PLUGIN_HOST_PLUGINABILOWERING_H
#define NEVERC_PLUGIN_HOST_PLUGINABILOWERING_H

#include "neverc/Plugin/Host/PluginTargetRegistry.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace neverc::plugin {

class PluginTaskContext;

struct PluginABIFunctionClassification {
  NevercABIArgumentClassification ReturnValue{};
  std::vector<NevercABIArgumentClassification> Arguments;
  uint32_t LLVMCallingConvention = 0;
};

class PluginABILowering {
public:
  PluginABILowering(
      const PluginTargetSnapshot::NamedRecord &ABI,
      const PluginTargetSnapshot::NamedRecord *CallingConvention,
      PluginTaskContext *Task = nullptr)
      : ABI(ABI), CallingConvention(CallingConvention), Task(Task) {}

  llvm::Expected<PluginABIFunctionClassification>
  classify(const NevercABITypeDescriptor &ReturnType,
           llvm::ArrayRef<NevercABITypeDescriptor> Parameters,
           bool Variadic, uint32_t RequiredArguments) const;

private:
  const PluginTargetSnapshot::NamedRecord &ABI;
  const PluginTargetSnapshot::NamedRecord *CallingConvention;
  PluginTaskContext *Task;
};

} // namespace neverc::plugin

#endif
