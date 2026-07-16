#include "neverc/Plugin/Host/PluginLLVMOptionSnapshot.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace neverc::plugin {
namespace {

struct ThreadGateOwnership {
  std::shared_mutex *ExclusiveGate = nullptr;
  unsigned ExclusiveDepth = 0;
  std::shared_mutex *SharedGate = nullptr;
  unsigned SharedDepth = 0;
};

thread_local ThreadGateOwnership GateOwnership;

} // namespace

std::shared_mutex &pluginLLVMOptionGate() {
  static std::shared_mutex Gate;
  return Gate;
}

bool pluginLLVMOptionGateHeldExclusivelyByCurrentThread() {
  return GateOwnership.ExclusiveGate == &pluginLLVMOptionGate() &&
         GateOwnership.ExclusiveDepth != 0;
}

bool pluginLLVMOptionGateHeldSharedByCurrentThread() {
  return GateOwnership.SharedGate == &pluginLLVMOptionGate() &&
         GateOwnership.SharedDepth != 0;
}

PluginLLVMOptionExclusiveLease::PluginLLVMOptionExclusiveLease(
    std::shared_mutex &GateValue)
    : Gate(&GateValue), Lock(GateValue, std::defer_lock) {
  if (GateOwnership.ExclusiveDepth != 0) {
    if (GateOwnership.ExclusiveGate != Gate)
      llvm::report_fatal_error(
          "nested LLVM option leases used different gates");
    ++GateOwnership.ExclusiveDepth;
    return;
  }
  if (GateOwnership.SharedDepth != 0 && GateOwnership.SharedGate == Gate)
    llvm::report_fatal_error("cannot upgrade a shared LLVM option lease");
  Lock.lock();
  GateOwnership.ExclusiveGate = Gate;
  GateOwnership.ExclusiveDepth = 1;
}

PluginLLVMOptionExclusiveLease::~PluginLLVMOptionExclusiveLease() {
  if (!Gate || GateOwnership.ExclusiveGate != Gate ||
      GateOwnership.ExclusiveDepth == 0)
    llvm::report_fatal_error("unbalanced exclusive LLVM option lease");
  --GateOwnership.ExclusiveDepth;
  if (GateOwnership.ExclusiveDepth == 0)
    GateOwnership.ExclusiveGate = nullptr;
}

PluginLLVMOptionSharedLease::PluginLLVMOptionSharedLease(
    std::shared_mutex &GateValue)
    : Gate(&GateValue), Lock(GateValue, std::defer_lock) {
  if (GateOwnership.ExclusiveDepth != 0 &&
      GateOwnership.ExclusiveGate == Gate) {
    CoveredByExclusive = true;
    return;
  }
  if (GateOwnership.SharedDepth != 0) {
    if (GateOwnership.SharedGate != Gate)
      llvm::report_fatal_error(
          "nested LLVM option leases used different gates");
    ++GateOwnership.SharedDepth;
    return;
  }
  Lock.lock();
  GateOwnership.SharedGate = Gate;
  GateOwnership.SharedDepth = 1;
}

PluginLLVMOptionSharedLease::~PluginLLVMOptionSharedLease() {
  if (CoveredByExclusive)
    return;
  if (!Gate || GateOwnership.SharedGate != Gate ||
      GateOwnership.SharedDepth == 0)
    llvm::report_fatal_error("unbalanced shared LLVM option lease");
  --GateOwnership.SharedDepth;
  if (GateOwnership.SharedDepth == 0)
    GateOwnership.SharedGate = nullptr;
}

PluginLLVMOptionSnapshot::PluginLLVMOptionSnapshot(std::shared_mutex &Gate)
    : Lock(Gate) {
  SmallPtrSet<cl::Option *, 32> Seen;
  auto Capture = [&](cl::SubCommand &Subcommand) {
    for (auto &Entry : cl::getRegisteredOptions(Subcommand)) {
      cl::Option *Option = Entry.second;
      if (Seen.insert(Option).second)
        Restorers.push_back(Option->createStateRestorer());
    }
  };

  Capture(cl::SubCommand::getTopLevel());
  for (cl::SubCommand *Subcommand : cl::getRegisteredSubcommands())
    Capture(*Subcommand);
}

PluginLLVMOptionSnapshot::~PluginLLVMOptionSnapshot() {
  for (auto It = Restorers.rbegin(); It != Restorers.rend(); ++It)
    (*It)();
}

} // namespace neverc::plugin
