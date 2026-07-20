#ifndef NEVERC_PLUGIN_HOST_CALLINGCONVENTIONMATERIALIZE_H
#define NEVERC_PLUGIN_HOST_CALLINGCONVENTIONMATERIALIZE_H

#include "llvm/Support/Error.h"

namespace llvm {
class Module;
class TargetMachine;
}

namespace neverc::plugin {

class PluginTaskContext;

llvm::Error materializeCallingConventionPlans(
    llvm::Module &Module, llvm::TargetMachine &TargetMachine,
    PluginTaskContext *Task);

} // namespace neverc::plugin

#endif
